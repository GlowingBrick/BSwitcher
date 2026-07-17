import { pathConfig } from './pathConfig.js';
import { AppLoader } from './appLoader.js';
import { ConfigManager } from './configManager.js';
import { UIManager } from './uiManager.js';

// 初始化主题
function initTheme() {
    const saved = localStorage.getItem('bswitcher-theme');
    if (saved) {
        document.documentElement.setAttribute('data-theme', saved);
    } else if (window.matchMedia('(prefers-color-scheme: light)').matches) {
        document.documentElement.setAttribute('data-theme', 'light');
    }
}

// 初始化应用
async function initApp() {
    initTheme();

    try {
        await pathConfig.load();
        const appLoader = new AppLoader();
        await appLoader.loadAppList();
        const configManager = new ConfigManager(appLoader);
        await configManager.init();
        const uiManager = new UIManager(configManager, appLoader);
        await uiManager.init();

        window.app = { pathConfig, appLoader, configManager, uiManager };
    } catch (error) {
        const rulesList = document.getElementById('rulesList');
        if (rulesList) {
            rulesList.innerHTML = `<div class="rules-empty" style="color:var(--danger)">初始化失败: ${error.message}</div>`;
        }
    }

    document.querySelectorAll('.card').forEach(c => c.classList.add('loaded'));
}

initApp();
