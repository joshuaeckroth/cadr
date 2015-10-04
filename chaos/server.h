/*
 * server.h
 * $Id: server.h 46 2005-10-01 12:02:45Z brad $
 */

#ifndef SERVER_H
#define SERVER_H

void server_input(struct connection *conn);
void start_file(void *conn, char *args, int arglen);


#endif



/*
 * Local Variables:
 * indent-tabs-mode:nil
 * c-basic-offset:4
 * End:
*/

