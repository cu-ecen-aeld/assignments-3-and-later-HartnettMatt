/**
 * @file main.c
 * @brief Functions and data related to the AESD char driver implementation
 *
 * Based on the implementation of the "scull" device driver, found in
 * Linux Device Drivers example code.
 *
 * @author Dan Walkes, Matt Hartnett
 * @date 2025-03-12
 * @copyright Copyright (c) 2025
 *
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/printk.h>
#include <linux/types.h>
#include <linux/cdev.h>
#include <linux/fs.h> // file_operations
#include <linux/uaccess.h>
#include "aesdchar.h"
#include "aesd-circular-buffer.h"
#include "aesd_ioctl.h"

int aesd_major =   0; // use dynamic major
int aesd_minor =   0;

MODULE_AUTHOR("Matt Hartnett");
MODULE_LICENSE("Dual BSD/GPL");

struct aesd_dev aesd_device;

int aesd_open(struct inode *inode, struct file *filp)
{
    PDEBUG("open");
    filp->private_data = &aesd_device;
    return 0;
}

int aesd_release(struct inode *inode, struct file *filp)
{
    PDEBUG("release");
    // TODO: handle release
    return 0;
}

ssize_t aesd_read(struct file *filp, char __user *buf, size_t count,
                loff_t *f_pos)
{
    ssize_t retval = 0;
    size_t total_size = 0;
    size_t read_offset = *f_pos;
    struct aesd_dev *dev = filp->private_data;
    struct aesd_buffer_entry *entry;
    size_t entry_offset = 0;
    size_t bytes_available = 0;
    size_t bytes_to_copy = 0;
    size_t bytes_copied = 0;
    int err;
    PDEBUG("read %zu bytes with offset %lld",count,*f_pos);
    mutex_lock(dev->mutex);
    // Calculate total size of all entries in the circular buffer
    {
        int i;
        int num_entries = dev->cbuf->full ? AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED :
                           (dev->cbuf->in_offs - dev->cbuf->out_offs);
        total_size = 0;
        for (i = 0; i < num_entries; i++) {
            int pos = (dev->cbuf->out_offs + i) % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;
            total_size += dev->cbuf->entry[pos].size;
        }
    }

    if (read_offset >= total_size) {
        mutex_unlock(dev->mutex);
        return 0; // EOF
    }

    // Use helper to locate the entry for current offset and copy out data
    while (count > 0 && (entry = aesd_circular_buffer_find_entry_offset_for_fpos(dev->cbuf, read_offset, &entry_offset)) != NULL) {
        bytes_available = entry->size - entry_offset;
        bytes_to_copy = (count < bytes_available) ? count : bytes_available;
        err = copy_to_user(buf, entry->buffptr + entry_offset, bytes_to_copy);
        if (err != 0) {
            mutex_unlock(dev->mutex);
            return -EFAULT;
        }
        buf += bytes_to_copy;
        count -= bytes_to_copy;
        read_offset += bytes_to_copy;
        bytes_copied += bytes_to_copy;
    }
    *f_pos = read_offset;
    mutex_unlock(dev->mutex);
    retval = bytes_copied;
    return retval;
}

ssize_t aesd_write(struct file *filp, const char __user *buf, size_t count,
                loff_t *f_pos)
{
    ssize_t retval = -ENOMEM;
    struct aesd_dev *dev = filp->private_data;
    char *kern_buf = NULL;
    char *new_cmd = NULL;
    size_t total_len;
    char *newline_ptr = NULL;
    size_t write_offset = 0;

    PDEBUG("write %zu bytes with offset %lld", count, *f_pos);

    kern_buf = kmalloc(count, GFP_KERNEL);
    if (!kern_buf)
        return -ENOMEM;
    if (copy_from_user(kern_buf, buf, count)) {
        kfree(kern_buf);
        return -EFAULT;
    }

    mutex_lock(dev->mutex);
    // Append new data to any existing pending data
    total_len = dev->pending_buf_size + count;
    new_cmd = kmalloc(total_len, GFP_KERNEL);
    if (!new_cmd) {
        kfree(kern_buf);
        mutex_unlock(dev->mutex);
        return -ENOMEM;
    }
    if (dev->pending_buf) {
        memcpy(new_cmd, dev->pending_buf, dev->pending_buf_size);
        kfree(dev->pending_buf);
    }
    memcpy(new_cmd + dev->pending_buf_size, kern_buf, count);
    kfree(kern_buf);
    dev->pending_buf = new_cmd;
    dev->pending_buf_size = total_len;

    // Process complete commands terminated by '\n'
    while ((newline_ptr = memchr(dev->pending_buf + write_offset, '\n', dev->pending_buf_size - write_offset)) != NULL) {
        size_t cmd_length = newline_ptr - (dev->pending_buf + write_offset) + 1;
        char *cmd_buf = kmalloc(cmd_length, GFP_KERNEL);
        if (!cmd_buf) {
            mutex_unlock(dev->mutex);
            return -ENOMEM;
        }
        memcpy(cmd_buf, dev->pending_buf + write_offset, cmd_length);

        {
            struct aesd_buffer_entry new_entry;
            new_entry.buffptr = cmd_buf;
            new_entry.size = cmd_length;
            // Add the entry and capture any replaced pointer
            const char *old_cmd = aesd_circular_buffer_add_entry(dev->cbuf, &new_entry);
            if (old_cmd)
                kfree(old_cmd);
        }
        write_offset += cmd_length;
    }

    // Handle incomplete command at the end
    if (write_offset < dev->pending_buf_size) {
        size_t leftover = dev->pending_buf_size - write_offset;
        char *temp_buf = kmalloc(leftover, GFP_KERNEL);
        if (!temp_buf) {
            mutex_unlock(dev->mutex);
            return -ENOMEM;
        }
        memcpy(temp_buf, dev->pending_buf + write_offset, leftover);
        kfree(dev->pending_buf);
        dev->pending_buf = temp_buf;
        dev->pending_buf_size = leftover;
    } else {
        kfree(dev->pending_buf);
        dev->pending_buf = NULL;
        dev->pending_buf_size = 0;
    }
    mutex_unlock(dev->mutex);
    retval = count;
    return retval;
}

// New llseek implementation to support SEEK_SET, SEEK_CUR, and SEEK_END
static loff_t aesd_llseek(struct file *filp, loff_t offset, int whence)
{
    struct aesd_dev *dev = filp->private_data;
    loff_t newpos;
    loff_t total_size = 0;
    int i, num_entries;

    PDEBUG("llseek: offset=%lld, whence=%d", offset, whence);

    mutex_lock(dev->mutex);
    num_entries = dev->cbuf->full ? AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED :
                   (dev->cbuf->in_offs - dev->cbuf->out_offs);
    for (i = 0; i < num_entries; i++) {
        int pos = (dev->cbuf->out_offs + i) % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;
        total_size += dev->cbuf->entry[pos].size;
    }
    switch (whence) {
        case SEEK_SET:
            newpos = offset;
            break;
        case SEEK_CUR:
            newpos = filp->f_pos + offset;
            break;
        case SEEK_END:
            newpos = total_size + offset;
            break;
        default:
            mutex_unlock(dev->mutex);
            return -EINVAL;
    }
    if (newpos < 0 || newpos > total_size) {
        mutex_unlock(dev->mutex);
        return -EINVAL;
    }
    filp->f_pos = newpos;
    mutex_unlock(dev->mutex);
    return newpos;
}

// New unlocked_ioctl implementation for AESDCHAR_IOCSEEKTO command support
static long aesd_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
    struct aesd_dev *dev = filp->private_data;
    long retval = 0;
    struct aesd_seekto seekto;

    if (_IOC_TYPE(cmd) != AESD_IOC_MAGIC) {
        return -ENOTTY;
    }
    if (_IOC_NR(cmd) > AESDCHAR_IOC_MAXNR) {
        return -ENOTTY;
    }
    switch(cmd) {
        case AESDCHAR_IOCSEEKTO:
            if (copy_from_user(&seekto, (const void __user *) arg, sizeof(seekto))) {
                retval = -EFAULT;
                break;
            }
            mutex_lock(dev->mutex);
            {
                int num_entries = dev->cbuf->full ? AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED :
                                  (dev->cbuf->in_offs - dev->cbuf->out_offs);
                if (seekto.write_cmd >= num_entries) {
                    retval = -EINVAL;
                } else {
                    int index = (dev->cbuf->out_offs + seekto.write_cmd) %
                                AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;
                    if (seekto.write_cmd_offset >= dev->cbuf->entry[index].size) {
                        retval = -EINVAL;
                    } else {
                        loff_t newpos = 0;
                        int i;
                        for (i = 0; i < seekto.write_cmd; i++) {
                            int pos = (dev->cbuf->out_offs + i) %
                                      AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;
                            newpos += dev->cbuf->entry[pos].size;
                        }
                        newpos += seekto.write_cmd_offset;
                        filp->f_pos = newpos;
                        PDEBUG("ioctl: set f_pos to %lld", newpos);
                    }
                }
            }
            mutex_unlock(dev->mutex);
            break;
        default:
            retval = -ENOTTY;
            break;
    }
    return retval;
}

struct file_operations aesd_fops = {
    .owner =    THIS_MODULE,
    .read =     aesd_read,
    .write =    aesd_write,
    .open =     aesd_open,
    .release =  aesd_release,
    .llseek =   aesd_llseek,
    .unlocked_ioctl = aesd_ioctl,
};

static int aesd_setup_cdev(struct aesd_dev *dev)
{
    int err, devno = MKDEV(aesd_major, aesd_minor);

    cdev_init(dev->cdev, &aesd_fops);
    dev->cdev->owner = THIS_MODULE;
    dev->cdev->ops = &aesd_fops;
    err = cdev_add(dev->cdev, devno, 1);
    if (err) {
        printk(KERN_ERR "Error %d adding aesd cdev", err);
    }
    return err;
}



int aesd_init_module(void)
{
    dev_t dev = 0;
    int result;
    result = alloc_chrdev_region(&dev, aesd_minor, 1,
            "aesdchar");
    aesd_major = MAJOR(dev);
    if (result < 0) {
        printk(KERN_WARNING "Can't get major %d\n", aesd_major);
        return result;
    }
    memset(&aesd_device,0,sizeof(struct aesd_dev));

    // Initialize the AESD specific portion of the device
    aesd_device.mutex = kmalloc(sizeof(struct mutex), GFP_KERNEL);
	if (!aesd_device.mutex) {
    	return -ENOMEM;
    }
    mutex_init(aesd_device.mutex);
    aesd_device.cbuf = kmalloc(sizeof(struct aesd_circular_buffer), GFP_KERNEL);
    if (!aesd_device.cbuf) {
        unregister_chrdev_region(dev, 1);
        return -ENOMEM;
    }
    aesd_circular_buffer_init(aesd_device.cbuf);
    aesd_device.pending_buf = NULL;
    aesd_device.pending_buf_size = 0;

    aesd_device.cdev = kmalloc(sizeof(struct cdev), GFP_KERNEL);
    if (!aesd_device.cdev) {
        unregister_chrdev_region(dev, 1);
        return -ENOMEM;
    }
    result = aesd_setup_cdev(&aesd_device);

    if( result ) {
        unregister_chrdev_region(dev, 1);
    }
    return result;
}

void aesd_cleanup_module(void)
{
    dev_t devno = MKDEV(aesd_major, aesd_minor);

    cdev_del(aesd_device.cdev);

    // Cleanup AESD specific poritions - circular buffer and pending buffer
    kfree(aesd_device.mutex);
    for (int i = 0; i < AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED; i++) {
        if (aesd_device.cbuf->entry[i].buffptr) {
            kfree(aesd_device.cbuf->entry[i].buffptr);
            aesd_device.cbuf->entry[i].buffptr = NULL;
            aesd_device.cbuf->entry[i].size = 0;
        }
    }
    // Free any pending buffer
    if (aesd_device.pending_buf) {
        kfree(aesd_device.pending_buf);
        aesd_device.pending_buf = NULL;
        aesd_device.pending_buf_size = 0;
    }
    kfree(aesd_device.cbuf);
    kfree(aesd_device.cdev);
    unregister_chrdev_region(devno, 1);
}

module_init(aesd_init_module);
module_exit(aesd_cleanup_module);
