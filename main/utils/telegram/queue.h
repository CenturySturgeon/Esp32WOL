#pragma once
#ifndef NOTIFS_QUEUE_H
#define NOTIFS_QUEUE_H

#include <stdbool.h>

void initialize_notifications_queue(void);
bool post_message_to_queue(const char *fmt, bool silent, ...);

#endif // NOTIFS_QUEUE_H
