#pragma once
#include "session.h"

#include <string>

struct GLFWwindow;

bool start_script(const std::string& path, std::string& error);
bool script_running();
void script_before_frame(Session& session);
void script_after_frame(GLFWwindow* window, Session& session);
int script_exit_code();
