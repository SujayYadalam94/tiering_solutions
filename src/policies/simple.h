/*
 * =====================================================================================
 *
 *       Filename:  simple.h
 *
 *    Description:  
 *
 *        Version:  1.0
 *        Created:  02/04/2020 09:56:26 AM
 *       Revision:  none
 *       Compiler:  gcc
 *
 *         Author:  YOUR NAME (), 
 *   Organization:  
 *
 * =====================================================================================
 */
#ifndef ARMS_SIMPLE_H
#define ARMS_SIMPLE_H

#include <stdint.h>
#include <stdbool.h>

#include "../arms.h"
#include "paging.h"

struct arms_page* simple_pagefault(void);
void simple_init(void);
void simple_remove_page(struct arms_page *page);
void simple_stats();

#endif // ARMS_SIMPLE_H
