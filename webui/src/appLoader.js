import { SocketClient } from './socketClient.js';

// 应用列表加载器 - 使用Socket通信
export class AppLoader {
    constructor() {
        this.availableApps = [];
        this.socketClient = new SocketClient();
    }

    // 从Socket加载应用列表
    async loadAppList() {
        try {

            const appList = await this.socketClient.readAppList();

            // 转换格式，确保数据一致性
            this.availableApps = appList.map(app => ({
                packageName: app.package_name,
                name: app.app_name
            }));

            return this.availableApps;

        } catch (error) {
            // 回退到内部文件
            return await this.loadInternalAppList();
        }
    }

    // 加载内部应用列表（备用）
    async loadInternalAppList() {
        try {
            // 从内部文件加载
            const response = await fetch('./data/applist.json');
            if (!response.ok) {
                throw new Error(`加载应用列表失败: ${response.status}`);
            }

            const appList = await response.json();
            this.availableApps = appList.map(app => ({
                packageName: app.package_name,
                name: app.app_name
            }));

            return this.availableApps;

        } catch (error) {
            // 返回空数组而不是抛出错误，让应用可以继续运行
            this.availableApps = [];
            return [];
        }
    }

    // 根据包名获取应用名称
    getAppName(packageName) {
        const app = this.availableApps.find(app => app.packageName === packageName);
        return app ? app.name : packageName;
    }

    // 获取所有应用列表
    getAvailableApps() {
        return this.availableApps;
    }

    // 搜索应用
    searchApps(keyword) {
        if (!keyword) return this.availableApps;

        return this.availableApps.filter(app =>
        app.name.toLowerCase().includes(keyword.toLowerCase()) ||
        app.packageName.toLowerCase().includes(keyword.toLowerCase())
        );
    }

    // 重新加载应用列表
    async reloadAppList() {
        this.availableApps = [];
        return await this.loadAppList();
    }
}
