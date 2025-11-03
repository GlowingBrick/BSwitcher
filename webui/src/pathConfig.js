import { SocketClient } from './socketClient.js';

// 路径配置管理器 - 现在从后端获取可用模式
class PathConfig {
    constructor() {
        this.paths = null;
        this.socketClient = new SocketClient();
    }

    // 加载路径配置
    async load() {
        try {
            console.log('正在从后端加载可用模式...');
            
            // 从后端获取可用模式
            const request = {
                target: 'availableModes',
                mode: 'read'
            };
            
            const availableModes = await this.socketClient.communicate(request);
            
            this.paths = {
                availableModes: availableModes || ['powersave', 'balance', 'performance', 'fast']
            };
            
            console.log('从后端加载可用模式成功:', this.paths.availableModes);
            return this.paths;
            
        } catch (error) {
            console.log('从后端加载可用模式失败，使用默认模式:', error);
            
            // 备用方案：使用默认模式
            this.paths = {
                availableModes: ['powersave', 'balance', 'performance', 'fast']
            };
            
            return this.paths;
        }
    }

    // 获取可用模式列表
    getAvailableModes() {
        if (!this.paths) {
            throw new Error('配置未加载，请先调用 load() 方法');
        }
        return this.paths.availableModes;
    }
}

// 创建单例实例
export const pathConfig = new PathConfig();