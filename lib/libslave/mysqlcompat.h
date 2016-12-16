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

#if MYSQL_VERSION_ID > 50541 && defined(MARIADB_BASE_VERSION)
/* MariaDB-5.5.42 or later, with low-level mysql_net_ API */
#else
#define mysql_net_read_packet cli_safe_read
#define mysql_net_field_length net_field_length
#endif

#ifdef DBUG_ASSERT
#undef DBUG_ASSERT
#endif
#define DBUG_ASSERT(A) do { assert(A); } while(0)
