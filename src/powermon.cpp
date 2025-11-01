#include <powermon.hpp>
bool init_power_sensors() {
    current_fd = open("/sys/class/power_supply/battery/current_now", O_RDONLY | O_CLOEXEC);
    voltage_fd = open("/sys/class/power_supply/battery/voltage_now", O_RDONLY | O_CLOEXEC);
    status_fd = open("/sys/class/power_supply/battery/status", O_RDONLY | O_CLOEXEC);
    return (current_fd >= 0) && (voltage_fd >= 0) && (status_fd >= 0);
}

std::unordered_map<std::string, AppPower> app_power_map;
int current_fd = -1;
int voltage_fd = -1;
int status_fd = -1;


// 读取当前功率（瓦特）
float read_current_power_w() {
    char buf[32] = {0};  // 初始化为0确保安全
    
    // 使用pread避免文件指针移动
    if (pread(current_fd, buf, sizeof(buf)-1, 0) <= 0) return 0.0f;
    long current_ua = atol(buf);
    if (current_ua<=0){
        return 0;
    }
    
    memset(buf, 0, sizeof(buf));
    if (pread(voltage_fd, buf, sizeof(buf)-1, 0) <= 0) return 0.0f;
    long voltage_uv = atol(buf);
    
    // 计算功率：P = (I * V) / 1e12 (单位转换：μA * μV → W)
    return static_cast<float>(static_cast<double>(current_ua) * static_cast<double>(voltage_uv) * 1e-12f);
}

bool dump_power_stats(const std::string& filename) {
    const std::string tmp_filename = filename + ".d";
    
    {
        std::ofstream outfile(tmp_filename, std::ios::trunc);
        if (!outfile.is_open()) {
            LOGE("Failed to create temp file: %s", tmp_filename.c_str());
            return false;
        }

        outfile << std::fixed << std::setprecision(2);

        for (const auto& [name, stats] : app_power_map) {
            int rounded_time = static_cast<int>(std::round(stats.time_sec));
            if (rounded_time < 1) continue;

            outfile << name << " "
                   << stats.power_joules << " "
                   << rounded_time << "\n";
        }

        // 显式关闭确保数据刷入磁盘
        outfile.close();
        if (outfile.fail()) {
            LOGE("Failed to write temp file: %s", tmp_filename.c_str());
            unlink(tmp_filename.c_str());
            return false;
        }
    }

    // 原子重命名操作
    if (rename(tmp_filename.c_str(), filename.c_str()) != 0) {
        LOGE("Failed to rename %s to %s: %s", 
             tmp_filename.c_str(), filename.c_str(), strerror(errno));
        unlink(tmp_filename.c_str());
        return false;
    }
    LOGI("Powerfile dumped.");
    return true;
}

// 主监控线程
void* s_thread(void* arg) {
    SharedData* shared = static_cast<SharedData*>(arg);

    // 初始化传感器
    if (!init_power_sensors()) {
        LOGE("Unable to open Battery files.");
        return nullptr;
    }
    char BTChar;
    float power_w;
    float delta_t;
    // 初始化时间
    timespec current_time;
    timespec last_time = current_time;  
    clock_gettime(CLOCK_MONOTONIC, &last_time);
    while (true) {
        sleep(1); // 基础间隔

         if (unlikely(access(POWER_OUTPUT_FILE.c_str(), F_OK) != 0)) {
            dump_power_stats(POWER_OUTPUT_FILE);
         }

        // 获取前台应用
        pthread_mutex_lock(&shared->mutex);
        std::string app_name = *(shared->content); 
        pthread_mutex_unlock(&shared->mutex);
        
        pread(status_fd, &BTChar, 1,0);
        if(BTChar=='C' || app_name.empty() || app_name=="none"){  //充电、熄屏时不统计
            sleep(1);
            clock_gettime(CLOCK_MONOTONIC, &last_time);
            continue; // 跳过统计
        }

        // 读取当前功率
        power_w = read_current_power_w();

        clock_gettime(CLOCK_MONOTONIC, &current_time);

        delta_t = (static_cast<float>(current_time.tv_sec - last_time.tv_sec)+
                         static_cast<float>(static_cast<double>(current_time.tv_nsec - last_time.tv_nsec) * 1e-9)); //过程使用double,保护精度

        last_time = current_time;
        // 更新应用统计
        AppPower& stats = app_power_map[app_name];
        stats.time_sec  += delta_t;
        stats.power_joules += power_w * delta_t;

    }
    
    // 清理
    close(current_fd);
    close(voltage_fd);
    close(status_fd);
    return nullptr;
}

