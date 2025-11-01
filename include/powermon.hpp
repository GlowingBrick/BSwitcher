#ifndef POWERMON
#define POWERMON

#include <iostream>
#include <fstream>
#include <string>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <cerrno>
#include <ctime>
#include <cmath>
#include <map>
#include <iomanip>
#include <pthread.h>
#include <Alog.hpp>


float read_current_power_w();
bool dump_power_stats(const std::string& filename);
void* s_thread(void* arg);

#endif