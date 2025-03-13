/**
 * @file aesd-circular-buffer.c
 * @brief Functions and data related to a circular buffer imlementation
 *
 * @author Dan Walkes, Matt Hartnett
 * @date 2025-03-04
 * @copyright Copyright (c) 2020
 *
 */

#ifdef __KERNEL__
#include <linux/string.h>
#else
#include <string.h>
#endif

#include "aesd-circular-buffer.h"

/**
 * @param buffer the buffer to search for corresponding offset.  Any necessary locking must be performed by caller.
 * @param char_offset the position to search for in the buffer list, describing the zero referenced
 *      character index if all buffer strings were concatenated end to end
 * @param entry_offset_byte_rtn is a pointer specifying a location to store the byte of the returned aesd_buffer_entry
 *      buffptr member corresponding to char_offset.  This value is only set when a matching char_offset is found
 *      in aesd_buffer.
 * @return the struct aesd_buffer_entry structure representing the position described by char_offset, or
 * NULL if this position is not available in the buffer (not enough data is written).
 */
struct aesd_buffer_entry *aesd_circular_buffer_find_entry_offset_for_fpos(struct aesd_circular_buffer *buffer,
            size_t char_offset, size_t *entry_offset_byte_rtn )
{
    uint8_t idx = buffer->out_offs;
    size_t cumulative_size = 0;
    uint8_t entries_checked = 0;

    // We can iterate up to AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED times.
    while (entries_checked < AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED) {
        // If we haven't stored anything, break if not full and idx reaches in_offs.
        if ((!buffer->full) && (idx == buffer->in_offs)) {
            break;
        }

        if (char_offset < (cumulative_size + buffer->entry[idx].size)) {
            // The desired offset lies within this entry.
            *entry_offset_byte_rtn = char_offset - cumulative_size;
            return &buffer->entry[idx];
        }

        cumulative_size += buffer->entry[idx].size;
        idx = (idx + 1) % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;
        entries_checked++;
    }

    // If we get here, it means we could not find the offset.
    return NULL;
}

/**
* Adds entry @param add_entry to @param buffer in the location specified in buffer->in_offs.
* If the buffer was already full, overwrites the oldest entry and advances buffer->out_offs to the
* new start location.
* Any necessary locking must be handled by the caller
* Any memory referenced in @param add_entry must be allocated by and/or must have a lifetime managed by the caller.
* @return NULL or, if an existing entry at out_offs was replaced, the value of buffptr for the entry
* which was replaced (for use with dynamic memory allocation/freeing)
*/
const char *aesd_circular_buffer_add_entry(struct aesd_circular_buffer *buffer, const struct aesd_buffer_entry *add_entry)
{
    const char *replaced_ptr = NULL;
    if (buffer->full) {
        // Save the pointer to the oldest entry to be replaced
        replaced_ptr = buffer->entry[buffer->out_offs].buffptr;
        // When the buffer is full, the out_offs entry is about to be overwritten,
        // so move out_offs forward.
        buffer->out_offs = (buffer->out_offs + 1) % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;
    }

    // Write the new entry into the in_offs position.
    buffer->entry[buffer->in_offs] = *add_entry;

    // Move in_offs forward.
    buffer->in_offs = (buffer->in_offs + 1) % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;

    // If we caught up to out_offs, the buffer is now full.
    if (buffer->in_offs == buffer->out_offs) {
        buffer->full = true;
    } else {
        buffer->full = false;
    }

    return replaced_ptr;
}

/**
* Initializes the circular buffer described by @param buffer to an empty struct
*/
void aesd_circular_buffer_init(struct aesd_circular_buffer *buffer)
{
    memset(buffer, 0, sizeof(struct aesd_circular_buffer));
}
