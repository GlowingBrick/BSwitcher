// UI管理器
import { InfoManager } from './infoManager.js';
import { pathConfig } from './pathConfig.js';
import { SocketClient } from './socketClient.js';

export class UIManager {
    constructor(configManager, appLoader) {
        this.configManager = configManager;
        this.socketClient =new SocketClient();
        this.appLoader = appLoader;
        this.infoManager = new InfoManager();
        
        this.modal = document.getElementById('addRuleModal');
        this.configModal = document.getElementById('configModal');
        this.powerModal = document.getElementById('powerDataModal');
        this.logsModal = document.getElementById('logsModal');

        this.allApps = [];
        this.refreshBtn = null;
        this.rulesList = null;
        this.pendingDeleteAppPackage = null;
        this.deleteTimeout = null;
        
        this.configFields = [];
        this.configCategories = {};

        this.availableFps = [-1]; // 默认包含-1
    }

    // 初始化UI
    async init() {
        await this.loadConfigFields();
        await this.loadSystemInfo();
        await this.loadAvailableFps();
        this.bindEvents();
        this.renderDefaultMode();
        this.renderRulesList();
        this.populateModeSelects();
        this.allApps = this.appLoader.getAvailableApps();
        this.populateAppSelect();
    }

    async loadAvailableFps() {
        try {
            const request = {
                target: 'dynamicFps',
                mode: 'read'
            };
            const fpsList = await this.socketClient.communicate(request);
            
            this.availableFps = [-1, ...fpsList.map(fps => Number(fps))];
            console.log('可用FPS列表:', this.availableFps);
        } catch (error) {
            console.error('加载FPS列表失败:', error);
            this.availableFps = [-1, 30, 60, 90, 120]; 
        }
    }

    // 加载配置字段定义
    async loadConfigFields() {
        try {
            await this.infoManager.loadConfigFields();
            this.configFields = this.infoManager.getConfigFields();
            this.configCategories = this.groupConfigByCategory();
        } catch (error) {
            this.configFields = [];
            this.configCategories = {};
        }
    }

    // 加载系统信息
    async loadSystemInfo() {
        try {
            await this.infoManager.loadSystemInfo();
            this.renderSystemInfo();
        } catch (error) {
        }
    }

    // 渲染系统信息
    renderSystemInfo() {
        const systemInfo = this.infoManager.getSystemInfo();
        const systemInfoElement = document.getElementById('systemInfo');
        
        if (systemInfoElement && systemInfo) {
            systemInfoElement.innerHTML = `
                <div class="app-name">${systemInfo.name || 'BSwitcher'}</div>
                <div class="app-meta">
                    <div>作者: ${systemInfo.author || 'Unknown'}</div>
                    <div>版本: ${systemInfo.version || '1.0.0'}</div>
                </div>
            `;
        }
    }

    // 绑定事件
    bindEvents() {
        // 添加规则按钮
        document.getElementById('addRuleBtn').addEventListener('click', () => {
            this.showModal();
        });

        // 模态框关闭按钮
        document.querySelector('.close').addEventListener('click', () => {
            this.hideModal();
        });

        // 取消按钮
        document.getElementById('cancelBtn').addEventListener('click', () => {
            this.hideModal();
        });

        // 保存规则按钮
        document.getElementById('saveRuleBtn').addEventListener('click', () => {
            this.saveRule();
        });

        // 日志按钮事件
        document.getElementById('showLogsBtn').addEventListener('click', () => {
            this.showLogsModal();
        });

        document.querySelector('.close-logs').addEventListener('click', () => {
            this.hideLogsModal();
        });

        document.getElementById('closeLogsBtn').addEventListener('click', () => {
            this.hideLogsModal();
        });

        // 默认模式变更
        document.getElementById('defaultMode').addEventListener('change', (e) => {
            this.configManager.updateDefaultMode(e.target.value);
        });

        // 功耗列表按钮
        document.getElementById('showPowerDataBtn').addEventListener('click', () => {
            this.showPowerDataModal();
        });

        document.querySelector('.close-power').addEventListener('click', () => {
            this.hidePowerDataModal();
        });

        document.getElementById('closePowerBtn').addEventListener('click', () => {
            this.hidePowerDataModal();
        });

        // 点击模态框外部关闭
        this.modal.addEventListener('click', (e) => {
            if (e.target === this.modal) {
                this.hideModal();
            }
        });

        // 搜索框输入事件
        const appSearch = document.getElementById('appSearch');
        if (appSearch) {
            appSearch.addEventListener('input', (e) => {
                this.searchApps(e.target.value);
            });
        }

        // 刷新应用列表按钮
        this.refreshBtn = document.getElementById('refreshAppListBtn');
        if (this.refreshBtn) {
            this.refreshBtn.addEventListener('click', () => {
                this.refreshAppList();
            });
        }

        // 为规则列表容器绑定事件委托
        this.rulesList = document.getElementById('rulesList');
        if (this.rulesList) {
            this.rulesList.addEventListener('click', (e) => {
                // 检查点击的是否是删除按钮
                if (e.target.classList.contains('delete-rule')) {
                    const appPackage = e.target.getAttribute('data-app-package');
                    if (appPackage) {
                        this.handleDeleteClick(appPackage, e.target);
                    }
                }
            });
        }

        // 配置页面事件
        document.getElementById('showConfigBtn').addEventListener('click', () => {
            this.showConfigModal();
        });

        document.querySelector('.close-config').addEventListener('click', () => {
            this.hideConfigModal();
        });

        document.getElementById('cancelConfigBtn').addEventListener('click', () => {
            this.hideConfigModal();
        });

        document.getElementById('saveConfigBtn').addEventListener('click', () => {
            this.saveConfig();
        });

    }


    // 显示日志模态框
    async showLogsModal() {
        try {
            this.logsModal.style.display = 'block';
            this.showLogsLoading();
            
            // 加载日志
            const logs = await this.loadLogs();
            this.renderLogs(logs);
            
            // 确保滚动到底部
            setTimeout(() => {
                this.scrollLogsToBottom();
            }, 100);
            
        } catch (error) {
            this.showLogsError('加载日志失败: ' + error.message);
        }
    }

    // 隐藏日志模态框
    hideLogsModal() {
        if (this.logsModal) {
            this.logsModal.style.display = 'none';
        }
    }

    // 加载日志
    async loadLogs() {
        const { exec } = await import('./ksu.js');
        
        // 更新日志命令
        const command = `logcat -v time -d -s "BSwitcher" | sed -E 's/^[0-9]*-[0-9]* //; s/\\.[0-9]*//; s/\\/[^\\(]*\\([^)]*\\):/:/'`;
        
        const result = await exec(command);
        
        if (result.errno === 0) {
            return result.stdout || '暂无日志';
        } else {
            throw new Error(result.stderr || '获取日志失败');
        }
    }

    // 显示日志加载状态
    showLogsLoading() {
        const logsContent = document.getElementById('logsContent');
        if (logsContent) {
            logsContent.innerHTML = '<div class="logs-loading">正在加载日志...</div>';
        }
    }

    // 显示日志错误
    showLogsError(message) {
        const logsContent = document.getElementById('logsContent');
        if (logsContent) {
            logsContent.innerHTML = `<div class="logs-error">${message}</div>`;
        }
    }

    // 渲染日志内容
    renderLogs(logs) {
        const logsContent = document.getElementById('logsContent');
        if (!logsContent) return;

        if (!logs) {
            logsContent.textContent = '暂无日志';
            return;
        }

        // 直接显示原始日志内容
        logsContent.textContent = logs;
        
        // 滚动到底部
        this.scrollLogsToBottom();
    }

    // 滚动日志到底部
    scrollLogsToBottom() {
        const logsContent = document.getElementById('logsContent');
        if (logsContent) {
            logsContent.scrollTop = logsContent.scrollHeight;
        }
    }

    // 删除按钮处理
    handleDeleteClick(appPackage, button) {
        // 如果已经有待删除的规则
        if (this.pendingDeleteAppPackage === appPackage) {
            // 第二次点击：确认删除
            this.executeDelete(appPackage);
            return;
        }

        // 清除之前的超时
        if (this.deleteTimeout) {
            clearTimeout(this.deleteTimeout);
        }

        // 第一次点击：进入确认状态
        this.pendingDeleteAppPackage = appPackage;
        
        // 保存原始文本和样式
        const originalText = button.textContent;
        const originalClass = button.className;
        
        // 更新按钮状态
        button.textContent = '确认删除？';
        button.className = 'btn btn-warning delete-rule';
        button.setAttribute('data-app-package', appPackage);

        // 设置超时，5秒后恢复
        this.deleteTimeout = setTimeout(() => {
            this.cancelDelete(button, originalText, originalClass);
        }, 5000);

        // 存储原始状态到按钮数据属性
        button.setAttribute('data-original-text', originalText);
        button.setAttribute('data-original-class', originalClass);
    }

    // 取消删除
    cancelDelete(button, originalText, originalClass) {
        if (button) {
            button.textContent = originalText || '删除';
            button.className = originalClass || 'btn btn-danger delete-rule';
        }
        this.pendingDeleteAppPackage = null;
        this.deleteTimeout = null;
    }

    // 执行删除
    async executeDelete(appPackage) {
        // 清除超时
        if (this.deleteTimeout) {
            clearTimeout(this.deleteTimeout);
            this.deleteTimeout = null;
        }

        this.showToast('正在删除规则...');

        const rule = this.configManager.getConfig().rules.find(r => r.appPackage === appPackage);
        if (!rule) {
            this.showToast('错误: 未找到要删除的规则');
            this.pendingDeleteAppPackage = null;
            return;
        }

        try {
            await this.configManager.removeRule(appPackage);
            const appName = this.appLoader.getAppName(appPackage);
            this.showToast(`已删除规则: ${appName}`);
            this.renderRulesList();
        } catch (error) {
            this.showToast('删除规则失败: ' + error.message);
        } finally {
            this.pendingDeleteAppPackage = null;
        }
    }

    // 功耗列表功能
    async showPowerDataModal() {
        try {
            this.powerModal.style.display = 'block';
            this.showPowerLoading();
            
            // 加载功耗数据
            const powerData = await this.loadPowerData();
            this.renderPowerData(powerData);
            
        } catch (error) {
            this.showPowerError('加载功耗数据失败: ' + error.message);
        }
    }

    // 隐藏功耗数据模态框
    hidePowerDataModal() {
        if (this.powerModal) {
            this.powerModal.style.display = 'none';
        }
    }

    // 加载功耗数据
    async loadPowerData() {
        
        const request = {
            target: 'powerdata',
            mode: 'read'
        };

        const result = await this.socketClient.communicate(request);

        return result;

    }

    // 显示功耗加载状态
    showPowerLoading() {
        const powerList = document.getElementById('powerList');
        const powerSummary = document.getElementById('powerSummary');
        
        if (powerList) {
            powerList.innerHTML = '<div class="power-loading">正在加载功耗数据...</div>';
        }
        
        if (powerSummary) {
            powerSummary.innerHTML = '<div class="power-loading">正在计算统计信息...</div>';
        }
    }

    // 显示功耗错误
    showPowerError(message) {
        const powerList = document.getElementById('powerList');
        const powerSummary = document.getElementById('powerSummary');
        
        if (powerList) {
            powerList.innerHTML = `<div class="power-error">${message}</div>`;
        }
        
        if (powerSummary) {
            powerSummary.innerHTML = `<div class="power-error">${message}</div>`;
        }
    }

    // 渲染功耗数据
    renderPowerData(powerData) {
        if (!powerData || !Array.isArray(powerData)) {
            this.showPowerError('无效的功耗数据格式');
            return;
        }

        // 处理数据：计算功率、转换应用名称、排序
        const processedData = this.processPowerData(powerData);
        
        // 渲染总体统计
        this.renderPowerSummary(processedData);
        
        // 渲染应用列表
        this.renderPowerList(processedData);
    }

    // 处理功耗数据
    processPowerData(powerData) {
        // 先过滤掉 _other_ 条目，用于排名显示
        const filteredData = powerData.filter(item => item.name !== '_other_');
        
        return filteredData
            .map(item => {
                const powerJoules = parseFloat(item.power_joules) || 0;
                const timeSec = parseFloat(item.time_sec) || 0;
                const powerWatt = timeSec > 0 ? powerJoules / timeSec : 0;
                
                return {
                    packageName: item.name,
                    appName: this.appLoader.getAppName(item.name),
                    powerJoules,
                    timeSec,
                    powerWatt
                };
            })
            .filter(item => item.timeSec > 0) // 过滤掉时间为0的数据
            .sort((a, b) => b.powerJoules - a.powerJoules) // 按能耗排序
            .slice(0, 10); // 取前10名
    }

    // 渲染总体统计
    renderPowerSummary(processedData) {
        const powerSummary = document.getElementById('powerSummary');
        if (!powerSummary) return;

        // 计算总体统计
        const totalTime = processedData.reduce((sum, item) => sum + item.timeSec, 0);
        const totalEnergy = processedData.reduce((sum, item) => sum + item.powerJoules, 0);
        const avgPower = totalTime > 0 ? totalEnergy / totalTime : 0;

        // 格式化数值
        const formatNumber = (num, decimals = 1) => num.toFixed(decimals);
        const formatTime = (seconds) => {
            const hours = Math.floor(seconds / 3600);
            const minutes = Math.floor((seconds % 3600) / 60);
            return `${hours}小时${minutes}分钟`;
        };

        powerSummary.innerHTML = `
            <div class="power-summary-stats">
                <div class="power-stat-item">
                    <div class="power-stat-value">${formatNumber(totalEnergy)}</div>
                    <div class="power-stat-label">能耗 (焦耳)</div>
                </div>
                <div class="power-stat-item">
                    <div class="power-stat-value">${formatTime(totalTime)}</div>
                    <div class="power-stat-label">使用时间</div>
                </div>
                <div class="power-stat-item">
                    <div class="power-stat-value">${formatNumber(avgPower)}</div>
                    <div class="power-stat-label">平均功率 (瓦特)</div>
                </div>
            </div>
        `;
    }

    // 渲染功耗列表
    renderPowerList(processedData) {
        const powerList = document.getElementById('powerList');
        if (!powerList) return;

        if (processedData.length === 0) {
            powerList.innerHTML = '<div class="power-loading">暂无功耗数据</div>';
            return;
        }

        powerList.innerHTML = processedData.map((item, index) => `
            <div class="power-item">
                <div class="power-item-info">
                    <div class="power-app-name" title="${item.appName}">${index + 1}. ${item.appName}</div>
                    <div class="power-app-details">包名: ${item.packageName}</div>
                </div>
                <div class="power-app-stats">
                    <div class="power-watt">${item.powerWatt.toFixed(1)} W</div>
                    <div class="power-details">
                        ${item.powerJoules.toFixed(1)} J / ${this.formatTimeShort(item.timeSec)}
                    </div>
                </div>
            </div>
        `).join('');
    }

    // 格式化时间
    formatTimeShort(seconds) {
        const hours = Math.floor(seconds / 3600);
        const minutes = Math.floor((seconds % 3600) / 60);
        
        if (hours > 0) {
            return `${hours}h${minutes}m`;
        } else {
            return `${minutes}m`;
        }
    }

    // 刷新应用列表
    async refreshAppList() {
        if (!this.refreshBtn) return;

        // 禁用按钮并显示加载状态
        this.setRefreshButtonState('loading');

        try {
            // 重新加载应用列表
            await this.appLoader.reloadAppList();
            this.allApps = this.appLoader.getAvailableApps();
            this.populateAppSelect();

            // 显示成功状态
            this.setRefreshButtonState('success');
            this.showToast('应用列表刷新成功');

            // 2秒后恢复正常状态
            setTimeout(() => {
                this.setRefreshButtonState('normal');
            }, 2000);

        } catch (error) {

            // 显示错误状态
            this.setRefreshButtonState('error');
            this.showToast('刷新应用列表失败: ' + error.message);

            // 3秒后恢复正常状态
            setTimeout(() => {
                this.setRefreshButtonState('normal');
            }, 3000);
        }
    }

    // 设置刷新按钮状态
    setRefreshButtonState(state) {
        if (!this.refreshBtn) return;

        // 移除所有状态类
        this.refreshBtn.classList.remove('btn-loading', 'btn-success', 'btn-error');
        this.refreshBtn.disabled = false;

        switch (state) {
            case 'loading':
                this.refreshBtn.classList.add('btn-loading');
                this.refreshBtn.disabled = true;
                break;
            case 'success':
                this.refreshBtn.classList.add('btn-success');
                break;
            case 'error':
                this.refreshBtn.classList.add('btn-error');
                break;
            case 'normal':
            default:
                // 恢复正常状态
                break;
        }
    }

    // 显示模态框
    async showModal() {
        if (this.modal) {
            this.modal.style.display = 'block';
            // 重置表单
            const appSearch = document.getElementById('appSearch');
            const appSelect = document.getElementById('appSelect');
            const modeSelect = document.getElementById('modeSelect');
            const downFpsSelect = document.getElementById('downFpsSelect');
            const upFpsSelect = document.getElementById('upFpsSelect');
            const downFpsLabel = document.querySelector('label[for="downFpsSelect"]');
            const upFpsLabel = document.querySelector('label[for="upFpsSelect"]');

            if (appSearch) appSearch.value = '';
            if (appSelect) appSelect.value = '';
            if (modeSelect) modeSelect.value = this.configManager.getConfig().defaultMode;
            
            // 设置整数值，而不是字符串
            if (downFpsSelect) downFpsSelect.value = -1;
            if (upFpsSelect) upFpsSelect.value = -1;

            try {
                const request = {
                    target: 'config',
                    mode: 'read'
                };
                const result = await this.socketClient.communicate(request);
                const isDynamicFpsEnabled = result.dynamic_fps || false;
                
                const shouldShowFpsControls = isDynamicFpsEnabled;  //在未启用动态fps时不显示选择框
                
                if (downFpsSelect) downFpsSelect.style.display = shouldShowFpsControls ? 'block' : 'none';
                if (upFpsSelect) upFpsSelect.style.display = shouldShowFpsControls ? 'block' : 'none';
                if (downFpsLabel) downFpsLabel.style.display = shouldShowFpsControls ? 'block' : 'none';
                if (upFpsLabel) upFpsLabel.style.display = shouldShowFpsControls ? 'block' : 'none';
                
            } catch (error) {
                if (downFpsSelect) downFpsSelect.style.display = 'none';
                if (upFpsSelect) upFpsSelect.style.display = 'none';
                if (downFpsLabel) downFpsLabel.style.display = 'none';
                if (upFpsLabel) upFpsLabel.style.display = 'none';
            }

            // 显示所有应用
            this.populateAppSelect();
        }
    }

    // 隐藏模态框
    hideModal() {
        if (this.modal) {
            this.modal.style.display = 'none';
        }
    }

    // 填充应用选择框
    populateAppSelect(searchTerm = '') {
        const appSelect = document.getElementById('appSelect');
        const appCount = document.getElementById('appCount');

        if (!appSelect || !appCount) return;

        let availableApps = this.allApps;

        // 如果有搜索词，进行过滤
        if (searchTerm) {
            availableApps = this.appLoader.searchApps(searchTerm);
        }

        appSelect.innerHTML = '';

        if (availableApps.length === 0) {
            const option = document.createElement('option');
            option.value = '';
            option.textContent = searchTerm ? '未找到匹配的应用' : '暂无应用数据';
            option.disabled = true;
            appSelect.appendChild(option);
            appCount.textContent = searchTerm ? '未找到匹配的应用' : '暂无应用数据';
            return;
        }

        let enabledCount = 0;

        // 添加默认提示选项
        const defaultOption = document.createElement('option');
        defaultOption.value = '';
        defaultOption.textContent = '请选择应用';
        defaultOption.disabled = true;
        defaultOption.selected = true;
        appSelect.appendChild(defaultOption);

        availableApps.forEach(app => {
            // 检查是否已配置该应用
            const hasRule = this.configManager.hasRuleForApp(app.packageName);

            const option = document.createElement('option');
            option.value = app.packageName;

            // 简化显示文本
            const displayText = hasRule
                ? `${app.name} - 已配置`
                : app.name;

            option.textContent = displayText;
            option.disabled = hasRule;

            appSelect.appendChild(option);

            if (!hasRule) {
                enabledCount++;
            }
        });

        // 更新计数显示
        const totalCount = availableApps.length;
        const disabledCount = totalCount - enabledCount;
        let countText = `共 ${totalCount} 个应用`;

        if (searchTerm) {
            countText = `搜索到 ${totalCount} 个应用`;
        }

        if (disabledCount > 0) {
            countText += ` (${enabledCount} 个可用)`;
        }

        appCount.textContent = countText;
    }

    // 搜索应用
    searchApps(searchTerm) {
        this.populateAppSelect(searchTerm);
    }

    // 填充模式选择框
    populateModeSelects() {
        const defaultModeSelect = document.getElementById('defaultMode');
        const modeSelect = document.getElementById('modeSelect');
        const downFpsSelect = document.getElementById('downFpsSelect');
        const upFpsSelect = document.getElementById('upFpsSelect');

        if (!defaultModeSelect || !modeSelect || !downFpsSelect || !upFpsSelect) return;

        const availableModes = this.configManager.getAvailableModes();

        // 填充模式选择框
        [defaultModeSelect, modeSelect].forEach(select => {
            select.innerHTML = '';
            availableModes.forEach(mode => {
                const option = document.createElement('option');
                option.value = mode;
                option.textContent = mode;
                select.appendChild(option);
            });
        });

        // 填充FPS选择框
        [downFpsSelect, upFpsSelect].forEach(select => {
            select.innerHTML = '';
            this.availableFps.forEach(fps => {
                const option = document.createElement('option');
                option.value = fps; // 这里fps应该是数字，不是字符串
                option.textContent = fps === -1 ? '默认' : `${fps} FPS`;
                select.appendChild(option);
            });
        });

        // 设置默认模式当前值
        const config = this.configManager.getConfig();
        defaultModeSelect.value = config.defaultMode;
        
        // 设置默认FPS值
        downFpsSelect.value = -1;
        upFpsSelect.value = -1;
    }

    // 渲染默认模式
    renderDefaultMode() {
        const defaultModeSelect = document.getElementById('defaultMode');
        if (defaultModeSelect) {
            const config = this.configManager.getConfig();
            defaultModeSelect.value = config.defaultMode;
        }
    }

    // 渲染规则列表
    renderRulesList() {
        const rulesList = document.getElementById('rulesList');
        if (!rulesList) return;
        
        const config = this.configManager.getConfig();
        
        if (config.rules.length === 0) {
            rulesList.innerHTML = '<div class="empty-state">暂无规则，点击"添加规则"开始配置</div>';
            return;
        }

        rulesList.innerHTML = config.rules.map(rule => {
            const appName = this.appLoader.getAppName(rule.appPackage);
            const modeName = rule.mode;
            
            // 格式化FPS显示
            const formatFps = (fps) => fps === -1 ? '默认' : `${fps}`;
            const downFpsText = rule.down_fps !== undefined ? formatFps(rule.down_fps) : '默认';
            const upFpsText = rule.up_fps !== undefined ? formatFps(rule.up_fps) : '默认';

            let detailsContent;
            if (downFpsText === '默认' && upFpsText === '默认') {
                detailsContent = `模式: ${modeName}`;   //都为默认时不显示
            } else {
                detailsContent = `模式: ${modeName} | FPS: ${downFpsText} - ${upFpsText}`;
            }
            
            return `
                <div class="rule-item">
                    <div class="rule-info">
                        <div class="rule-name" title="${appName}">${appName}</div>
                        <div class="rule-details" title="${detailsContent}">
                            ${detailsContent}
                        </div>
                    </div>
                    <div class="rule-actions">
                        <button class="btn btn-danger delete-rule" data-app-package="${rule.appPackage}">删除</button>
                    </div>
                </div>
            `;
        }).join('');
        
        // 重置删除状态
        this.pendingDeleteAppPackage = null;
        if (this.deleteTimeout) {
            clearTimeout(this.deleteTimeout);
            this.deleteTimeout = null;
        }
    }

    // 保存规则
    async saveRule() {
        const appSelect = document.getElementById('appSelect');
        const modeSelect = document.getElementById('modeSelect');
        const downFpsSelect = document.getElementById('downFpsSelect');
        const upFpsSelect = document.getElementById('upFpsSelect');

        if (!appSelect || !modeSelect || !downFpsSelect || !upFpsSelect) return;

        const appPackage = appSelect.value;
        const mode = modeSelect.value;
        
        // 确保FPS值为整数类型
        const downFps = downFpsSelect.value === '' ? -1 : parseInt(downFpsSelect.value);
        const upFps = upFpsSelect.value === '' ? -1 : parseInt(upFpsSelect.value);

        if (!appPackage) {
            this.showToast('请选择应用');
            return;
        }

        // 检查是否已存在该应用的规则
        if (this.configManager.hasRuleForApp(appPackage)) {
            this.showToast('该应用已存在规则配置');
            return;
        }

        try {
            await this.configManager.addRule(appPackage, mode, upFps, downFps);
            this.renderRulesList();
            this.hideModal();
        } catch (error) {
            this.showToast('保存规则失败: ' + error.message);
        }
    }

    // 配置页面相关方法
    async showConfigModal() {
        try {
            await this.infoManager.loadConfig();
            this.renderConfigForm();
            this.configModal.style.display = 'block';
        } catch (error) {
            this.showToast('加载配置失败: ' + error.message);
        }
    }

    // 隐藏配置模态框
    hideConfigModal() {
        if (this.configModal) {
            this.configModal.style.display = 'none';
        }
    }

    // 保存配置
    async saveConfig() {
        try {
            const formData = this.getConfigFormData();
            await this.infoManager.saveConfig(formData);
            this.showToast('配置保存成功');
            this.hideConfigModal();
        } catch (error) {
            this.showToast('保存配置失败: ' + error.message);
        }
    }

    // 渲染配置表单
    renderConfigForm() {
        const configForm = document.getElementById('configForm');
        if (!configForm) return;

        const config = this.infoManager.getConfig();
        
        // 按分类渲染
        configForm.innerHTML = Object.entries(this.configCategories).map(([category, fields]) => {
            const categoryFields = fields.map(field => this.renderConfigField(field, config)).join('');
            
            return `
                <div class="config-group">
                    <h4>${category}</h4>
                    ${categoryFields}
                </div>
            `;
        }).join('');

        // 绑定字段依赖关系的事件
        this.bindFieldDependencies();
    }

    // 渲染单个配置字段

    renderConfigField(field, config) {
        const value = config[field.key] !== undefined ? config[field.key] : '';
        const isDisabled = this.isFieldDisabled(field, config);
        const options = this.getFieldOptions(field, config);
        
        let fieldHtml = '';
        
        switch (field.type) {
            case 'number':
                fieldHtml = `
                    <input type="number" 
                        name="${field.key}" 
                        value="${value}" 
                        ${field.min ? `min="${field.min}"` : ''}
                        ${field.max ? `max="${field.max}"` : ''}
                        ${isDisabled ? 'disabled' : ''}>
                `;
                break;
                
            case 'checkbox':
                fieldHtml = `
                    <input type="checkbox" 
                        name="${field.key}" 
                        ${value ? 'checked' : ''}
                        ${isDisabled ? 'disabled' : ''}>
                `;
                break;
                
            case 'select':
                const optionHtml = options.map(opt => 
                    `<option value="${opt.value}" ${value === opt.value ? 'selected' : ''}>${opt.label}</option>`
                ).join('');
                fieldHtml = `
                    <select name="${field.key}" ${isDisabled ? 'disabled' : ''}>
                        ${optionHtml}
                    </select>
                `;
                break;
                
            case 'button':
                fieldHtml = `
                    <button type="button" 
                            class="btn btn-secondary btn-small" 
                            data-key="${field.key}"
                            ${isDisabled ? 'disabled' : ''}>
                        ${field.label}
                    </button>
                `;
                break;
                
            default: // text
                fieldHtml = `
                    <input type="text" 
                        name="${field.key}" 
                        value="${value}" 
                        ${isDisabled ? 'disabled' : ''}>
                `;
        }
        
        return `
            <div class="config-item ${isDisabled ? 'disabled' : ''}">
                <label>${field.type === 'button' ? '' : field.label}</label>
                ${fieldHtml}
                <div class="config-description">${field.description}</div>
            </div>
        `;
    }

    // 按分类分组配置字段
    groupConfigByCategory() {
        const categories = {};
        this.configFields.forEach(field => {
            const category = field.category || '其他设置';
            if (!categories[category]) {
                categories[category] = [];
            }
            categories[category].push(field);
        });
        return categories;
    }

    // 获取字段的选项列表
    getFieldOptions(field, currentConfig) {
        if (typeof field.options === 'string') {
            // 动态选项
            switch (field.options) {
                case 'availableModes':
                    return pathConfig.getAvailableModes().map(mode => ({
                        value: mode,
                        label: mode
                    }));
                case 'availableFps':
                    return this.availableFps.map(fps => ({
                        value: fps,
                        label: fps === -1 ? '默认' : `${fps} FPS`
                    }));
                default:
                    return [];
            }
        }
        return field.options || [];
    }

    // 检查字段是否应该禁用
    isFieldDisabled(field, currentConfig) {
        if (!field.dependsOn) return false;
        
        const { field: dependentField, condition } = field.dependsOn;

        if (currentConfig[dependentField] === undefined) {  // 忽略不存在的规则
            return false;
        }

        const dependentValue = currentConfig[dependentField];
        
        // 如果依赖字段的值不等于条件值，则禁用此字段
        return dependentValue !== condition;
    }

    // 绑定字段依赖关系事件
    bindFieldDependencies() {
        const configForm = document.getElementById('configForm');
        if (!configForm) return;

        // 为所有有affects属性的字段绑定change事件
        this.configFields.forEach(field => {
            if (field.affects && field.affects.length > 0) {
                const input = configForm.querySelector(`[name="${field.key}"]`);
                if (input) {
                    input.addEventListener('change', () => {
                        this.updateDependentFields();
                    });
                }
            }
        });

        // 为所有按钮类型字段绑定点击事件
        this.configFields.forEach(field => {
            if (field.type === 'button') {
                const button = configForm.querySelector(`button[data-key="${field.key}"]`);
                if (button) {
                    button.addEventListener('click', (event) => {
                        this.handleConfigButtonClick(field, event);
                    });
                }
            }
        });
    }

    // 更新依赖字段的状态
    updateDependentFields() {
        const config = this.getCurrentConfigFormData();
        
        this.configFields.forEach(field => {
            if (field.dependsOn) {
                const input = document.querySelector(`[name="${field.key}"]`);
                const container = input?.closest('.config-item');
                
                if (input && container) {
                    const isDisabled = this.isFieldDisabled(field, config);
                    
                    input.disabled = isDisabled;
                    if (isDisabled) {
                        container.classList.add('disabled');
                    } else {
                        container.classList.remove('disabled');
                    }
                }
            }
        });
    }

    // 处理配置按钮点击
    async handleConfigButtonClick(field, event) {
        const button = event.target;
        const key = field.key;
        
        // 检查是否需要确认
        const requireConfirmation = field.require_confirmation !== false; // 默认为true
        
        // 如果按钮已经在确认状态，直接执行
        if (button.classList.contains('confirming')) {
            await this.executeButtonAction(key, button);
            return;
        }
        
        // 如果需要确认
        if (requireConfirmation) {
            this.startConfirmationMode(button, field);
        } else {
            // 不需要确认，直接执行
            await this.executeButtonAction(key, button);
        }
    }

    // 启动确认模式
    startConfirmationMode(button, field) {
        // 保存原始状态
        button._originalText = button.textContent;
        button._originalClass = button.className;
        
        // 切换到确认状态
        button.classList.remove('btn-secondary');
        button.classList.add('btn-secondary-warning', 'confirming');
        button.textContent = '确认？';
        
        // 设置超时恢复
        button._confirmationTimeout = setTimeout(() => {
            this.cancelConfirmation(button);
        }, 5000);
    }

    // 取消确认模式
    cancelConfirmation(button) {
        if (button._originalClass) {
            button.className = button._originalClass;
        }
        if (button._originalText) {
            button.textContent = button._originalText;
        }
        button.classList.remove('confirming');
        
        if (button._confirmationTimeout) {
            clearTimeout(button._confirmationTimeout);
            button._confirmationTimeout = null;
        }
    }

    // 执行按钮动作
    async executeButtonAction(key, button) {
        this.cancelConfirmation(button);    //恢复状态
        
        try {
            const request = {
                target: 'command',
                mode: 'write',
                data: [key]
            };
            
            const result = await this.socketClient.communicate(request);
            if (result?.message && result.message.trim() !== '') {
                this.showToast(result.message);
            }
        } catch (error) {
            this.showToast('Unknown error: ' + error.message);
        }
    }

    // 获取当前表单数据
    getCurrentConfigFormData() {
        const form = document.getElementById('configForm');
        // 排除按钮类型的输入元素
        const inputs = form.querySelectorAll('input:not([type="button"]), select');
        const config = {};
        
        inputs.forEach(input => {
            if (input.type === 'checkbox') {
                config[input.name] = input.checked;
            } else if (input.type === 'number') {
                config[input.name] = parseInt(input.value) || 0;
            } else {
                config[input.name] = input.value;
            }
        });
        
        return config;
    }

    // 获取配置表单数据
    getConfigFormData() {
        return this.getCurrentConfigFormData();
    }


    // 显示Toast提示
    async showToast(message) {
        try {
            const { toast } = await import('./ksu.js');
            if (typeof ksu !== 'undefined') {
                toast(message);
            } else {
                // 在网页环境中使用alert作为备用
                alert(message);
            }
        } catch (error) {
            alert(message); // 最终备用方案
        }
    }
}