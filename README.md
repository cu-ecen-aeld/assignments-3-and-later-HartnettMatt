# ECEN5713 Assignment 2
This repo contains ECEN5713 assignment 2, completed by Matt Hartnett.

## Purpose
The purpose of this assignment was to re-familiarize myself with C, and learn how to create a makefile. 
It also provided a vector by which I can check the installation of the arm cross-compiler.
As part of this assignment, finder-test.sh was modified, and a couple of verification files were created in `assignments/assignment2/`


## 1. `finder.c`

### Description
This programs creates or overwrites a file with a specified string, assuming the directory path exists.

### Usage
```bash
gcc ./writer.c
./writer <writefile> <writestr>
```

---

## 2. `makefile`

### Description
This makefile can clean, compile, and cross compile the writer program.

### Usage
```bash
make clean
```
```bash
make all
```
```bash
make CROSS_COMPILE=aarch64-none-linux-gnu-
```
---
### Author: Matt Hartnett