#pragma once

/*
* Odyssey.
*
* Scalable PostgreSQL connection pooler.
*/

int read_attribute_buf(char **data, size_t *data_size, char attr_key,
		       char **out, size_t *out_size);

int read_any_attribute_buf(char **data, size_t *data_size, char *attribute_ptr,
			   char **out, size_t *out_size);
