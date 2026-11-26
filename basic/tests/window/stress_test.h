#pragma once
#include "session.h"

#include <string>

void start_stress_test(const std::string& system_lines);
bool stress_test_running();
void stress_before_frame(Session& session);
void stress_after_frame(Session& session, double frame_ms);
