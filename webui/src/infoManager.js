import { SocketClient } from './socketClient.js';

// 信息管理器
export class InfoManager {
    constructor() {
        this.socketClient = new SocketClient();
        this.systemInfo = null;
        this.config = null;
        this.configFields = null;
    }

    async loadConfigFields() {
        try {
            const request = {
                target: 'configlist',
                mode: 'read'
            };
            
            this.configFields = await this.socketClient.communicate(request);
            return this.configFields;
        } catch (error) {
            // 返回空数组而不是抛出错误
            this.configFields = [];
            return this.configFields;
        }
    }

    // 加载系统信息
    async loadSystemInfo() {
        try {
            const request = {
                target: 'info',
                mode: 'read'
            };
            
            this.systemInfo = await this.socketClient.communicate(request);
            return this.systemInfo;
        } catch (error) {
            // 返回默认信息
            this.systemInfo = {
                name: 'Unknown',
                author: 'Unknown',
                version: '1.0.0'
            };
            return this.systemInfo;
        }
    }

    // 加载配置
    async loadConfig() {
        try {
            const request = {
                target: 'config',
                mode: 'read'
            };
            
            this.config = await this.socketClient.communicate(request);
            return this.config;
        } catch (error) {
            // 返回默认配置
            this.config = this.getDefaultConfig();
            return this.config;
        }
    }

    // 保存配置
    async saveConfig(config) {
        try {
            const request = {
                target: 'config',
                mode: 'write',
                data: config
            };
            
            const response = await this.socketClient.communicate(request);
            
            if (response.status === 'success') {
                this.config = config;
                return true;
            } else {
                throw new Error(response.message || '保存配置失败');
            }
        } catch (error) {
            throw error;
        }
    }

    // 获取默认配置
    getDefaultConfig() {
        return {
            low_battery_threshold: 15,
            mode_file: "",
            poll_interval: 2,
            power_monitoring: true,
            scene: true,
            screen_off: "powersave"
        };
    }


    // 获取配置字段定义
    getConfigFields() {
        return this.configFields || [];
    }

    // 获取系统信息
    getSystemInfo() {
        return this.systemInfo;
    }

    // 获取配置
    getConfig() {
        return this.config;
    }
}