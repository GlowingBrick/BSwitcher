import { pathConfig } from './pathConfig.js';
import { AppLoader } from './appLoader.js';
import { ConfigManager } from './configManager.js';
import { UIManager } from './uiManager.js';

// 初始化应用
async function initApp() {
    try {
        console.log('正在初始化应用...');

        // 首先加载路径配置
        console.log('正在加载路径配置...');
        await pathConfig.load();

        // 初始化应用加载器
        const appLoader = new AppLoader();

        // 加载应用列表
        console.log('正在加载应用列表...');
        await appLoader.loadAppList();

        // 初始化配置管理器
        const configManager = new ConfigManager(appLoader);
        await configManager.init();

        // 初始化UI管理器
        const uiManager = new UIManager(configManager, appLoader);
        await uiManager.init();

        console.log('应用初始化完成');

        // 暴露到全局，方便调试
        window.app = {
            pathConfig,
            appLoader,
            configManager,
            uiManager
        };

    } catch (error) {
        console.error('应用初始化失败:', error);
        const rulesList = document.getElementById('rulesList');
        if (rulesList) {
            rulesList.innerHTML = `
            <div class="empty-state" style="color: #e74c3c;">
            初始化失败: ${error.message}<br>
            请检查浏览器控制台获取详细信息
            </div>
            `;
        }
    }
}

// 启动应用
initApp();
