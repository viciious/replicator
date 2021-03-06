
/* Copyright 2011 ZAO "Begun".
 *
 * This library is free software; you can redistribute it and/or modify it under
 * the terms of the GNU Lesser General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option)
 * any later version.
 * This library is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License for more
 * details.
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef __SLAVE_LOGGING_H
#define __SLAVE_LOGGING_H

#include <iostream>

#define LOG_TRACE(LOG, S)
#define LOG_DEBUG(LOG, S)
#define LOG_INFO(LOG, S)
#define LOG_WARNING(LOG, S) std::cout << S << std::endl;
#define LOG_ERROR(LOG, S) std::cerr << S << std::endl;

#endif
