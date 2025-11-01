import { pathConfig } from './pathConfig.js';
import { SocketClient } from './socketClient.js';

// 配置管理器 - 使用Socket通信
export class ConfigManager {
    constructor(appLoader) {
        this.appLoader = appLoader;
        this.config = {
            defaultMode: '',
                rules: []
        };
        this.socketClient = new SocketClient();
    }

    // 初始化配置管理器
    async init() {
        await this.loadConfig();
    }

    // 加载配置文件 - 使用Socket读取
    async loadConfig() {
        try {
            console.log('正在通过Socket加载配置...');

            const response = await this.socketClient.readScheduler();

            // 直接使用后端返回的配置
            this.config = {
                defaultMode: response.defaultMode || pathConfig.getAvailableModes()[0] || '',
                    rules: response.rules || []
            };

            console.log('配置加载成功:', this.config);

        } catch (error) {
            console.log('配置加载失败，使用默认配置:', error);
            this.config = {
                defaultMode: pathConfig.getAvailableModes()[0] || '',
                    rules: []
            };

            // 尝试保存默认配置
            try {
                await this.saveConfig();
            } catch (saveError) {
                console.error('保存默认配置失败:', saveError);
            }
        }
    }

    // 保存配置 - 使用Socket写入
    async saveConfig() {
        try {
            console.log('正在通过Socket保存配置...');

            // 准备要保存的配置数据（移除id字段）
            const configToSave = {
                defaultMode: this.config.defaultMode,
                    rules: this.config.rules.map(rule => ({
                        appPackage: rule.appPackage,
                        mode: rule.mode
                    }))
            };

            const response = await this.socketClient.writeScheduler(configToSave);

            console.log('配置保存成功');
            this.showToast('配置保存成功');

            return this.config;
        } catch (error) {
            console.error('保存配置时出错:', error);
            this.showToast('配置保存出错: ' + error.message);
            throw error;
        }
    }

    // 显示Toast提示
    async showToast(message) {
        try {
            const { toast } = await import('./ksu.js');
            if (typeof ksu !== 'undefined') {
                toast(message);
            } else {
                console.log('Toast (模拟):', message);
            }
        } catch (error) {
            console.log('Toast显示失败:', error);
        }
    }

    // 添加规则
    async addRule(appPackage, mode) {
        const appName = this.appLoader.getAppName(appPackage);

        const rule = {
            appPackage, // 包名作为唯一标识
            mode
        };
        
        // 检查是否已存在该应用的规则，如果存在则更新
        const existingIndex = this.config.rules.findIndex(rule => rule.appPackage === appPackage);
        if (existingIndex >= 0) {
            this.config.rules[existingIndex] = rule;
            this.showToast(`已更新规则: ${appName}`);
        } else {
            this.config.rules.push(rule);
            this.showToast(`已添加规则: ${appName}`);
        }
        
        await this.saveConfig();
        return rule;
    }


    // 删除规则
    async removeRule(appPackage) {
        console.log('ConfigManager: 删除规则', appPackage);
        
        // 查找要删除的规则
        const ruleToRemove = this.config.rules.find(rule => rule.appPackage === appPackage);
        if (!ruleToRemove) {
            throw new Error(`规则 ${appPackage} 不存在`);
        }
        
        // 过滤掉要删除的规则
        this.config.rules = this.config.rules.filter(rule => rule.appPackage !== appPackage);
        console.log('删除后剩余规则:', this.config.rules.length);
        
        // 保存配置
        await this.saveConfig();
        
        return ruleToRemove;
    }

    // 更新默认模式
    async updateDefaultMode(mode) {
        this.config.defaultMode = mode;
        await this.saveConfig();
        this.showToast(`默认模式已设置为: ${mode}`);
    }

    // 获取配置数据
    getConfig() {
        return this.config;
    }

    // 获取可用模式列表
    getAvailableModes() {
        return pathConfig.getAvailableModes();
    }

    // 检查应用是否已有规则
    hasRuleForApp(appPackage) {
        return this.config.rules.some(rule => rule.appPackage === appPackage);
    }
}
