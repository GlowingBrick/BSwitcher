#ifndef MAIN_HPP
#define MAIN_HPP
#include "JSONSocket/JSONSocket.hpp"
#include <memory>
#include <filesystem>
#include <iostream>
#include <sched.h>
#include <ForegroundApp.hpp>
#include <inotifywatcher.hpp>
#include <thread>
#include <chrono>
#include <JSONSocketModule/ApplistModule.hpp>
#include <JSONSocketModule/ConfigModule.hpp>
#include <JSONSocketModule/InformationModule.hpp>
#include <JSONSocketModule/MonitorModule.hpp>
#define unlikely(x) __builtin_expect(!!(x), 0)

#endif