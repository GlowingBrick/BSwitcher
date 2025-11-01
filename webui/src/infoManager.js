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
            console.log('正在加载配置字段定义...');
            const request = {
                target: 'configlist',
                mode: 'read'
            };
            
            this.configFields = await this.socketClient.communicate(request);
            console.log('配置字段定义加载成功:', this.configFields);
            return this.configFields;
        } catch (error) {
            console.error('加载配置字段定义失败:', error);
            // 返回空数组而不是抛出错误
            this.configFields = [];
            return this.configFields;
        }
    }

    // 加载系统信息
    async loadSystemInfo() {
        try {
            console.log('正在加载系统信息...');
            const request = {
                target: 'info',
                mode: 'read'
            };
            
            this.systemInfo = await this.socketClient.communicate(request);
            console.log('系统信息加载成功:', this.systemInfo);
            return this.systemInfo;
        } catch (error) {
            console.error('加载系统信息失败:', error);
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
            console.log('正在加载系统配置...');
            const request = {
                target: 'config',
                mode: 'read'
            };
            
            this.config = await this.socketClient.communicate(request);
            console.log('系统配置加载成功:', this.config);
            return this.config;
        } catch (error) {
            console.error('加载系统配置失败:', error);
            // 返回默认配置
            this.config = this.getDefaultConfig();
            return this.config;
        }
    }

    // 保存配置
    async saveConfig(config) {
        try {
            console.log('正在保存系统配置...');
            const request = {
                target: 'config',
                mode: 'write',
                data: config
            };
            
            const response = await this.socketClient.communicate(request);
            
            if (response.status === 'success') {
                this.config = config;
                console.log('系统配置保存成功');
                return true;
            } else {
                throw new Error(response.message || '保存配置失败');
            }
        } catch (error) {
            console.error('保存系统配置失败:', error);
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