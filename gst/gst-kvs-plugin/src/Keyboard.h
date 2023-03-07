#ifndef __KEYBOARD_H__
#define __KEYBOARD_H__

#include <stdbool.h>
#include "View.h"

CameraView getNewViewByKey(const char* keyCode);
bool moveViewWithKey(const char* key);

#endif  // __KEYBOARD_H__
