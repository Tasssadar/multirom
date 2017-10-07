/*
 * This file is part of MultiROM.
 *
 * MultiROM is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * MultiROM is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with MultiROM.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef _RQ_INJECT_FILE_CONTEXTS_H
#define _RQ_INJECT_FILE_CONTEXTS_H

/*
 * Main inject_file_contexts() function will determine
 * if the file is a compiled binary format or text format
 * and act accordingly.
 */
int inject_file_contexts(const char *path);

#endif //_RQ_INJECT_FILE_CONTEXTS_H
