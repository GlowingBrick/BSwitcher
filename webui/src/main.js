import { pathConfig } from './pathConfig.js';
import { AppLoader } from './appLoader.js';
import { ConfigManager } from './configManager.js';
import { UIManager } from './uiManager.js';

// 初始化应用
async function initApp() {
    try {
        await pathConfig.load();    //虽然名字还叫路径配置，但实际上只有模式列表。
                                    //“历史遗留问题”，没改纯粹是懒

        // 初始化应用加载器
        const appLoader = new AppLoader();

        // 加载应用列表
        await appLoader.loadAppList();

        // 初始化配置管理器
        const configManager = new ConfigManager(appLoader);
        await configManager.init();

        // 初始化UI管理器
        const uiManager = new UIManager(configManager, appLoader);
        await uiManager.init();

        window.app = {
            pathConfig,
            appLoader,
            configManager,
            uiManager
        };

    } catch (error) {
        const rulesList = document.getElementById('rulesList');
        if (rulesList) {
            rulesList.innerHTML = `
            <div class="empty-state" style="color: #e74c3c;">
            初始化失败: ${error.message}<br>
            </div>
            `;
        }
    }
}

// 启动应用
initApp();
