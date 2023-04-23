/**
 * Copyright (C) 2023 Andrea Pepe <pepe.andmj@gmail.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 * @file dir_ops.c
 * @brief directory operations for the single-file file-system hosting
 * the BLDMS block device. Only the iterate on the directory is implemented.
 *  
 * @author Andrea Pepe
 * 
 * @date April 22, 2023  
*/

#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/timekeeping.h>
#include <linux/time.h>
#include <linux/buffer_head.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/string.h>

#include "include/bldms.h"

/*
 * this function handles only the index of one out of 3 possible outcomes:
 * - 0: the directory "."
 * - 1: the directory ".."
 * - 2: the single file of the filesytem
 */
static int bldms_iterate(struct file *file, struct dir_context *ctx){
	if (ctx->pos >= (2 + 1)){
		return 0;
	}

	if (ctx->pos == 0){
		if (!dir_emit_dot(file, ctx)){
			return 0;
		}else{
			ctx->pos++;
		}
	}else if (ctx->pos == 1){
		if(!dir_emit_dotdot(file, ctx)){
			return 0;
		}
		else{
			ctx->pos++;
		}
	}else{
		if(!dir_emit(ctx, UNIQUE_FILE_NAME, UNIQUE_FILE_NAME_SIZE, BLDMS_SINGLEFILE_INODE_NUMBER, DT_UNKNOWN)){
			return 0;
		}
		else{
			ctx->pos++;
		}
	}
	return 0;
}

const struct file_operations bldms_dir_operations = {
	.owner = THIS_MODULE,
	.iterate = bldms_iterate,
};
