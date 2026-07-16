// UI管理器
import { InfoManager } from './infoManager.js';
import { pathConfig } from './pathConfig.js';
import { SocketClient } from './socketClient.js';
import { CustomDropdown } from './customDropdown.js';

export class UIManager {
    constructor(configManager, appLoader) {
        this.configManager = configManager;
        this.socketClient = new SocketClient();
        this.appLoader = appLoader;
        this.infoManager = new InfoManager();

        this.modalOverlay = document.getElementById('modalOverlay');
        this.modal = document.getElementById('addRuleModal');
        this.configModal = document.getElementById('configModal');
        this.powerModal = document.getElementById('powerDataModal');
        this.logsModal = document.getElementById('logsModal');
        this.appPickerModal = document.getElementById('appPickerModal');

        this.allApps = [];
        this.refreshBtn = null;
        this.rulesList = null;
        this.pendingDeleteAppPackage = null;
        this.deleteTimeout = null;

        this.configFields = [];
        this.configCategories = {};

        this.availableFps = [-1];

        // 自定义下拉框实例
        this.defaultModeDropdown = null;
        this.modeDropdown = null;
        this.downFpsDropdown = null;
        this.upFpsDropdown = null;

        // 应用选择器状态
        this.selectedAppPackage = '';
        this.selectedAppName = '';
    }

    async init() {
        await this.loadConfigFields();
        await this.loadSystemInfo();
        await this.loadAvailableFps();
        this.initDefaultModeDropdown();
        this.bindEvents();
        this.renderDefaultMode();
        this.renderRulesList();
        this.allApps = this.appLoader.getAvailableApps();
    }

    async loadAvailableFps() {
        try {
            const request = { target: 'dynamicFps', mode: 'read' };
            const fpsList = await this.socketClient.communicate(request);
            this.availableFps = [-1, ...fpsList.map(fps => Number(fps))];
        } catch {
            this.availableFps = [-1, 30, 60, 90, 120];
        }
    }

    async loadConfigFields() {
        try {
            await this.infoManager.loadConfigFields();
            this.configFields = this.infoManager.getConfigFields();
            this.configCategories = this.groupConfigByCategory();
        } catch {
            this.configFields = [];
            this.configCategories = {};
        }
    }

    async loadSystemInfo() {
        try {
            await this.infoManager.loadSystemInfo();
            this.renderSystemInfo();
        } catch { /* silent */ }
    }

    renderSystemInfo() {
        const systemInfo = this.infoManager.getSystemInfo();
        const el = document.getElementById('systemInfo');
        if (el && systemInfo) {
            el.innerHTML = `
                <div class="info-name">${systemInfo.name || 'BSwitcher'}</div>
                <div class="info-meta">
                    <span>${systemInfo.author || 'Unknown'}</span>
                    <span>v${systemInfo.version || '1.0.0'}</span>
                </div>`;
        }
    }

    // ========== 事件绑定 ==========
    bindEvents() {
        // 添加规则
        document.getElementById('addRuleBtn').addEventListener('click', () => this.showModal());
        document.getElementById('cancelBtn').addEventListener('click', () => this.hideModal());
        document.getElementById('saveRuleBtn').addEventListener('click', () => this.saveRule());

        // 应用选择器弹窗
        document.getElementById('appPickerBtn').addEventListener('click', () => this.showAppPicker());
        document.getElementById('appPickerSearch').addEventListener('input', (e) => this.filterAppPicker(e.target.value));
        // 应用选择器的关闭按钮只关自己
        this.appPickerModal.querySelector('[data-close]').addEventListener('click', (e) => {
            e.stopPropagation();
            this.hideAppPicker();
        });

        // 日志
        document.getElementById('showLogsBtn').addEventListener('click', () => this.showLogsModal());
        document.getElementById('closeLogsBtn').addEventListener('click', () => this.hideLogsModal());

        // 默认模式
        document.getElementById('defaultMode').addEventListener('change', (e) => {
            this.configManager.updateDefaultMode(e.target.value);
        });

        // 功耗
        document.getElementById('showPowerDataBtn').addEventListener('click', () => this.showPowerDataModal());
        document.getElementById('closePowerBtn').addEventListener('click', () => this.hidePowerDataModal());

        // 配置
        document.getElementById('showConfigBtn').addEventListener('click', () => this.showConfigModal());
        document.getElementById('cancelConfigBtn').addEventListener('click', () => this.hideConfigModal());
        document.getElementById('saveConfigBtn').addEventListener('click', () => this.saveConfig());

        // 主题切换
        document.getElementById('themeToggle').addEventListener('click', () => this.toggleTheme());

        // data-close 按钮统一处理
        this.modalOverlay.addEventListener('click', (e) => {
            const closeBtn = e.target.closest('[data-close]');
            if (closeBtn) {
                this.hideAllModals();
                return;
            }
            if (e.target === this.modalOverlay) {
                this.hideAllModals();
            }
        });

        // ESC关闭
        document.addEventListener('keydown', (e) => {
            if (e.key === 'Escape') this.hideAllModals();
        });

        // 刷新应用列表
        this.refreshBtn = document.getElementById('refreshAppListBtn');
        if (this.refreshBtn) {
            this.refreshBtn.addEventListener('click', () => this.refreshAppList());
        }

        // 规则列表事件委托
        this.rulesList = document.getElementById('rulesList');
        if (this.rulesList) {
            this.rulesList.addEventListener('click', (e) => {
                const btn = e.target.closest('.rule-del');
                if (btn) {
                    const pkg = btn.getAttribute('data-app-package');
                    if (pkg) this.handleDeleteClick(pkg, btn);
                }
            });
        }
    }

    // ========== 主题 ==========
    toggleTheme() {
        const html = document.documentElement;
        const current = html.getAttribute('data-theme');
        const next = current === 'dark' ? 'light' : 'dark';
        html.setAttribute('data-theme', next);
        localStorage.setItem('bswitcher-theme', next);
    }

    // ========== 模态框 ==========
    showOverlay() {
        this.modalOverlay.classList.add('active');
    }

    hideOverlay() {
        this.modalOverlay.classList.remove('active');
    }

    hideAllModals() {
        this.modal.style.display = 'none';
        this.logsModal.style.display = 'none';
        this.powerModal.style.display = 'none';
        this.configModal.style.display = 'none';
        if (this.appPickerModal) this.appPickerModal.style.display = 'none';
        this.hideOverlay();
    }

    showModal() {
        this.showOverlay();
        this.modal.style.display = 'block';

        // 初始化自定义下拉框
        this.initAddRuleDropdowns();

        // 检查是否显示FPS选择
        const downFpsParent = document.getElementById('downFpsSelect')?.closest('.form-group');
        const upFpsParent = document.getElementById('upFpsSelect')?.closest('.form-group');

        (async () => {
            try {
                const result = await this.socketClient.communicate({ target: 'config', mode: 'read' });
                const show = result.dynamic_fps || false;
                if (downFpsParent) downFpsParent.style.display = show ? '' : 'none';
                if (upFpsParent) upFpsParent.style.display = show ? '' : 'none';
            } catch {
                if (downFpsParent) downFpsParent.style.display = 'none';
                if (upFpsParent) upFpsParent.style.display = 'none';
            }
        })();
    }

    hideModal() {
        this.modal.style.display = 'none';
        this.hideOverlay();
    }

    // ========== 日志 ==========
    async showLogsModal() {
        this.showOverlay();
        this.logsModal.style.display = 'block';
        const logsContent = document.getElementById('logsContent');
        if (logsContent) logsContent.innerHTML = '<div class="logs-loading">正在加载日志...</div>';

        try {
            const { exec } = await import('./ksu.js');
            const command = `logcat -v time -d -s "BSwitcher" | sed -E 's/^[0-9]*-[0-9]* //; s/\\.[0-9]*//; s/\\/[^\\(]*\\([^)]*\\):/:/'`;
            const result = await exec(command);
            const logs = result.errno === 0 ? (result.stdout || '暂无日志') : (result.stderr || '获取日志失败');
            if (logsContent) {
                logsContent.textContent = logs;
                logsContent.scrollTop = logsContent.scrollHeight;
            }
        } catch (error) {
            if (logsContent) logsContent.innerHTML = `<div class="logs-error">加载日志失败: ${error.message}</div>`;
        }
    }

    hideLogsModal() {
        this.logsModal.style.display = 'none';
        this.hideOverlay();
    }

    // ========== 功耗 ==========
    async showPowerDataModal() {
        this.showOverlay();
        this.powerModal.style.display = 'block';
        const powerList = document.getElementById('powerList');
        const powerSummary = document.getElementById('powerSummary');
        const listSection = document.querySelector('.power-list-section');
        if (powerList) powerList.innerHTML = '<div class="logs-loading">正在加载功耗数据...</div>';
        if (powerSummary) powerSummary.innerHTML = '<div class="logs-loading">正在计算统计...</div>';

        // 移除旧的折叠状态
        if (powerSummary) powerSummary.classList.remove('collapsed');

        // 绑定滚动监听 - 列表滚动时折叠顶部统计
        if (listSection && powerSummary) {
            listSection.onscroll = () => {
                if (listSection.scrollTop > 10) {
                    powerSummary.classList.add('collapsed');
                } else {
                    powerSummary.classList.remove('collapsed');
                }
            };
        }

        try {
            const request = { target: 'powerdata', mode: 'read' };
            const powerData = await this.socketClient.communicate(request);
            this.renderPowerData(powerData);
        } catch (error) {
            if (powerList) powerList.innerHTML = `<div class="logs-error">${error.message}</div>`;
            if (powerSummary) powerSummary.innerHTML = `<div class="logs-error">${error.message}</div>`;
        }
    }

    hidePowerDataModal() {
        this.powerModal.style.display = 'none';
        this.hideOverlay();
    }

    renderPowerData(powerData) {
        if (!powerData || !Array.isArray(powerData)) {
            document.getElementById('powerList').innerHTML = '<div class="logs-error">无效数据格式</div>';
            return;
        }
        const processed = powerData
            .filter(item => item.name !== '_other_')
            .map(item => ({
                packageName: item.name,
                appName: this.appLoader.getAppName(item.name),
                powerJoules: parseFloat(item.power_joules) || 0,
                timeSec: parseFloat(item.time_sec) || 0,
                get powerWatt() { return this.timeSec > 0 ? this.powerJoules / this.timeSec : 0; }
            }))
            .filter(item => item.timeSec > 0)
            .sort((a, b) => b.powerJoules - a.powerJoules)
            .slice(0, 10);

        this.renderPowerSummary(processed);
        this.renderPowerList(processed);
    }

    renderPowerSummary(data) {
        const el = document.getElementById('powerSummary');
        if (!el) return;
        const totalTime = data.reduce((s, i) => s + i.timeSec, 0);
        const totalEnergy = data.reduce((s, i) => s + i.powerJoules, 0);
        const avgPower = totalTime > 0 ? totalEnergy / totalTime : 0;
        const fmtTime = (s) => `${Math.floor(s / 3600)}h${Math.floor((s % 3600) / 60)}m`;

        el.innerHTML = `
            <div class="power-summary-grid">
                <div class="power-stat">
                    <div class="power-stat-value">${totalEnergy.toFixed(1)}</div>
                    <div class="power-stat-label">能耗 (J)</div>
                </div>
                <div class="power-stat">
                    <div class="power-stat-value">${fmtTime(totalTime)}</div>
                    <div class="power-stat-label">使用时间</div>
                </div>
                <div class="power-stat">
                    <div class="power-stat-value">${avgPower.toFixed(1)} W</div>
                    <div class="power-stat-label">平均功率</div>
                </div>
            </div>`;
    }

    renderPowerList(data) {
        const el = document.getElementById('powerList');
        if (!el) return;
        if (data.length === 0) {
            el.innerHTML = '<div class="logs-loading">暂无功耗数据</div>';
            return;
        }
        const fmtTime = (s) => {
            const h = Math.floor(s / 3600);
            const m = Math.floor((s % 3600) / 60);
            return h > 0 ? `${h}h${m}m` : `${m}m`;
        };
        el.innerHTML = data.map((item, i) => `
            <div class="power-item">
                <div class="power-item-name">
                    <div class="app-name">${i + 1}. ${item.appName}</div>
                    <div class="pkg-name">${item.packageName}</div>
                </div>
                <div class="power-item-stats">
                    <div class="watt">${item.powerWatt.toFixed(1)} W</div>
                    <div class="detail">${item.powerJoules.toFixed(1)} J / ${fmtTime(item.timeSec)}</div>
                </div>
            </div>`).join('');
    }

    // ========== 删除规则 ==========
    handleDeleteClick(appPackage, button) {
        if (this.pendingDeleteAppPackage === appPackage) {
            this.executeDelete(appPackage);
            return;
        }
        if (this.deleteTimeout) clearTimeout(this.deleteTimeout);

        this.pendingDeleteAppPackage = appPackage;
        button._origText = button.textContent;
        button.textContent = '确认？';
        button.classList.add('confirming');

        this.deleteTimeout = setTimeout(() => {
            button.textContent = button._origText || '删除';
            button.classList.remove('confirming');
            this.pendingDeleteAppPackage = null;
            this.deleteTimeout = null;
        }, 5000);
    }

    async executeDelete(appPackage) {
        if (this.deleteTimeout) { clearTimeout(this.deleteTimeout); this.deleteTimeout = null; }
        this.showToast('正在删除规则...');
        try {
            await this.configManager.removeRule(appPackage);
            this.showToast(`已删除: ${this.appLoader.getAppName(appPackage)}`);
            this.renderRulesList();
        } catch (error) {
            this.showToast('删除失败: ' + error.message);
        } finally {
            this.pendingDeleteAppPackage = null;
        }
    }

    // ========== 刷新列表 ==========
    async refreshAppList() {
        if (!this.refreshBtn) return;
        this.setRefreshButtonState('loading');
        try {
            await this.appLoader.reloadAppList();
            this.allApps = this.appLoader.getAvailableApps();
            this.updateAppCount();
            this.setRefreshButtonState('success');
            this.showToast('应用列表刷新成功');
            setTimeout(() => this.setRefreshButtonState('normal'), 2000);
        } catch (error) {
            this.setRefreshButtonState('error');
            this.showToast('刷新失败: ' + error.message);
            setTimeout(() => this.setRefreshButtonState('normal'), 3000);
        }
    }

    setRefreshButtonState(state) {
        if (!this.refreshBtn) return;
        this.refreshBtn.classList.remove('btn-loading', 'btn-success', 'btn-error');
        this.refreshBtn.disabled = false;
        if (state === 'loading') { this.refreshBtn.classList.add('btn-loading'); this.refreshBtn.disabled = true; }
        else if (state === 'success') this.refreshBtn.classList.add('btn-success');
        else if (state === 'error') this.refreshBtn.classList.add('btn-error');
    }

    // ========== 模式选择 ==========
    initDefaultModeDropdown() {
        const container = document.getElementById('defaultMode');
        if (!container) return;

        const modes = this.configManager.getAvailableModes();
        const config = this.configManager.getConfig();

        this.defaultModeDropdown = new CustomDropdown(container, {
            items: modes.map(m => ({ value: m, label: m })),
            value: config.defaultMode,
            placeholder: '请选择模式',
            onChange: (value) => {
                this.configManager.updateDefaultMode(value);
            }
        });
    }

    renderDefaultMode() {
        if (this.defaultModeDropdown) {
            const config = this.configManager.getConfig();
            this.defaultModeDropdown.setValue(config.defaultMode);
        }
    }

    initAddRuleDropdowns() {
        const modes = this.configManager.getAvailableModes();
        const fpsItems = this.availableFps.map(f => ({ value: f, label: f === -1 ? '默认' : `${f} FPS` }));
        const config = this.configManager.getConfig();

        // 重置应用选择状态
        this.selectedAppPackage = '';
        this.selectedAppName = '';
        const label = document.getElementById('appPickerLabel');
        if (label) { label.textContent = '点击选择应用'; label.classList.add('placeholder'); }

        // 模式选择下拉框
        const modeContainer = document.getElementById('modeSelect');
        if (modeContainer) {
            this.modeDropdown = new CustomDropdown(modeContainer, {
                items: modes.map(m => ({ value: m, label: m })),
                value: config.defaultMode,
                placeholder: '请选择模式',
                onChange: () => {}
            });
        }

        // FPS下拉框
        const downContainer = document.getElementById('downFpsSelect');
        if (downContainer) {
            this.downFpsDropdown = new CustomDropdown(downContainer, {
                items: fpsItems,
                value: -1,
                placeholder: '选择空闲帧率',
                onChange: () => {}
            });
        }

        const upContainer = document.getElementById('upFpsSelect');
        if (upContainer) {
            this.upFpsDropdown = new CustomDropdown(upContainer, {
                items: fpsItems,
                value: -1,
                placeholder: '选择触摸帧率',
                onChange: () => {}
            });
        }

        // 更新应用计数
        this.updateAppCount();
    }

    updateAppCount(count) {
        const el = document.getElementById('appCount');
        if (el) {
            el.textContent = count !== undefined ? count : `共 ${this.allApps.length} 个应用`;
        }
    }

    // ========== 应用选择器弹窗 ==========
    showAppPicker() {
        this.appPickerModal.style.display = 'flex';
        this.renderAppPickerList('');
        const searchInput = document.getElementById('appPickerSearch');
        if (searchInput) { searchInput.value = ''; setTimeout(() => searchInput.focus(), 100); }
    }

    hideAppPicker() {
        this.appPickerModal.style.display = 'none';
    }

    renderAppPickerList(filter = '') {
        const listEl = document.getElementById('appPickerList');
        if (!listEl) return;
        const lf = filter.toLowerCase();
        const apps = filter
            ? this.allApps.filter(a => a.name.toLowerCase().includes(lf) || a.packageName.toLowerCase().includes(lf))
            : this.allApps;

        if (apps.length === 0) {
            listEl.innerHTML = '<div class="app-picker-empty">未找到匹配的应用</div>';
            return;
        }

        listEl.innerHTML = apps.map(app => {
            const hasRule = this.configManager.hasRuleForApp(app.packageName);
            const sel = app.packageName === this.selectedAppPackage ? ' selected' : '';
            return `<div class="app-picker-item${sel}" data-pkg="${app.packageName}">
                <span class="app-name">${app.name}${hasRule ? ' <span class="app-badge">已配置</span>' : ''}</span>
                <span class="app-pkg">${app.packageName}</span>
            </div>`;
        }).join('');

        listEl.querySelectorAll('.app-picker-item').forEach(el => {
            el.addEventListener('click', () => this.selectAppFromPicker(el.dataset.pkg));
        });
    }

    filterAppPicker(term) {
        this.renderAppPickerList(term);
    }

    selectAppFromPicker(pkg) {
        const app = this.allApps.find(a => a.packageName === pkg);
        if (!app) return;
        this.selectedAppPackage = pkg;
        this.selectedAppName = app.name;

        const label = document.getElementById('appPickerLabel');
        if (label) { label.textContent = app.name; label.classList.remove('placeholder'); }

        // 自动填充已有规则
        if (this.configManager.hasRuleForApp(pkg)) {
            const config = this.configManager.getConfig();
            const rule = config.rules.find(r => r.appPackage === pkg);
            if (rule) {
                if (this.modeDropdown) this.modeDropdown.setValue(rule.mode || config.defaultMode);
                if (this.downFpsDropdown) this.downFpsDropdown.setValue(rule.down_fps ?? -1);
                if (this.upFpsDropdown) this.upFpsDropdown.setValue(rule.up_fps ?? -1);
            }
        } else {
            const config = this.configManager.getConfig();
            if (this.modeDropdown) this.modeDropdown.setValue(config.defaultMode);
            if (this.downFpsDropdown) this.downFpsDropdown.setValue(-1);
            if (this.upFpsDropdown) this.upFpsDropdown.setValue(-1);
        }

        this.hideAppPicker();
    }

    // ========== 规则列表 ==========
    renderRulesList() {
        const el = document.getElementById('rulesList');
        if (!el) return;
        const config = this.configManager.getConfig();

        if (config.rules.length === 0) {
            el.innerHTML = '<div class="rules-empty">// 暂无规则，点击"+ 添加"开始配置</div>';
            this.pendingDeleteAppPackage = null;
            if (this.deleteTimeout) { clearTimeout(this.deleteTimeout); this.deleteTimeout = null; }
            return;
        }

        el.innerHTML = config.rules.map(rule => {
            const appName = this.appLoader.getAppName(rule.appPackage);
            const fmtFps = (fps) => fps === -1 ? '默认' : `${fps}`;
            const down = fmtFps(rule.down_fps ?? -1);
            const up = fmtFps(rule.up_fps ?? -1);
            const detail = (down === '默认' && up === '默认')
                ? `<span class="mode-tag">${rule.mode}</span>`
                : `<span class="mode-tag">${rule.mode}</span> · FPS ${down} → ${up}`;

            return `
            <div class="rule-row">
                <div class="rule-info">
                    <span class="rule-app" title="${appName}">${appName}</span>
                    <span class="rule-detail">${detail}</span>
                </div>
                <button class="rule-del" data-app-package="${rule.appPackage}">删除</button>
            </div>`;
        }).join('');

        this.pendingDeleteAppPackage = null;
        if (this.deleteTimeout) { clearTimeout(this.deleteTimeout); this.deleteTimeout = null; }
    }

    // ========== 保存规则 ==========
    async saveRule() {
        if (!this.selectedAppPackage || !this.modeDropdown || !this.downFpsDropdown || !this.upFpsDropdown) {
            if (!this.selectedAppPackage) this.showToast('请选择应用');
            return;
        }

        const appPackage = this.selectedAppPackage;
        const mode = this.modeDropdown.getValue();
        const downFps = this.downFpsDropdown.getValue() ?? -1;
        const upFps = this.upFpsDropdown.getValue() ?? -1;

        if (this.configManager.hasRuleForApp(appPackage)) {
            await this.configManager.removeRule(appPackage);
        }

        try {
            await this.configManager.addRule(appPackage, mode, upFps, downFps);
            this.renderRulesList();
            this.hideModal();
        } catch (error) {
            this.showToast('保存规则失败: ' + error.message);
        }
    }

    // ========== 配置 ==========
    async showConfigModal() {
        try {
            await this.infoManager.loadConfig();
            this.renderConfigForm();
            this.showOverlay();
            this.configModal.style.display = 'block';
        } catch (error) {
            this.showToast('加载配置失败: ' + error.message);
        }
    }

    hideConfigModal() {
        this.configModal.style.display = 'none';
        this.hideOverlay();
    }

    async saveConfig() {
        try {
            const formData = this.getCurrentConfigFormData();
            await this.infoManager.saveConfig(formData);
            this.showToast('配置保存成功');
            this.hideConfigModal();
        } catch (error) {
            this.showToast('保存配置失败: ' + error.message);
        }
    }

    renderConfigForm() {
        const form = document.getElementById('configForm');
        if (!form) return;
        const config = this.infoManager.getConfig();
        this.configDropdowns = {};

        form.innerHTML = Object.entries(this.configCategories).map(([cat, fields]) => {
            const items = fields.map(f => this.renderConfigField(f, config)).join('');
            return `<div class="config-group"><div class="config-group-title">${cat}</div>${items}</div>`;
        }).join('');

        // 初始化 select 类型的自定义下拉框
        this.configFields.forEach(field => {
            if (field.type === 'select') {
                const container = form.querySelector(`[data-dropdown-key="${field.key}"]`);
                if (container) {
                    const value = config[field.key] !== undefined ? config[field.key] : '';
                    const options = this.getFieldOptions(field, config);
                    const isDisabled = this.isFieldDisabled(field, config);
                    this.configDropdowns[field.key] = new CustomDropdown(container, {
                        items: options,
                        value: value,
                        placeholder: '请选择',
                        onChange: (val) => {
                            this.onConfigFieldChange(field.key, val);
                        }
                    });
                    if (isDisabled) container.closest('.config-item')?.classList.add('disabled');
                }
            }
        });

        this.bindFieldDependencies();
    }

    renderConfigField(field, config) {
        const value = config[field.key] !== undefined ? config[field.key] : '';
        const isDisabled = this.isFieldDisabled(field, config);
        const options = this.getFieldOptions(field, config);
        let inputHtml = '';

        switch (field.type) {
            case 'number':
                inputHtml = `<input type="number" class="input-text" name="${field.key}" value="${value}" ${field.min ? `min="${field.min}"` : ''} ${field.max ? `max="${field.max}"` : ''} ${isDisabled ? 'disabled' : ''}>`;
                break;
            case 'checkbox':
                inputHtml = `<label style="display:flex;align-items:center;gap:8px;cursor:pointer"><input type="checkbox" name="${field.key}" ${value ? 'checked' : ''} ${isDisabled ? 'disabled' : ''}> <span style="font-size:12px;color:var(--text-secondary)">${field.description}</span></label>`;
                break;
            case 'select':
                inputHtml = `<div data-dropdown-key="${field.key}" class="dropdown-wrap"></div>`;
                break;
            case 'button':
                inputHtml = `<button type="button" class="btn btn-ghost config-btn" data-key="${field.key}" ${isDisabled ? 'disabled' : ''}>${field.label}</button>`;
                break;
            default:
                inputHtml = `<input type="text" class="input-text" name="${field.key}" value="${value}" ${isDisabled ? 'disabled' : ''}>`;
        }

        const labelHtml = field.type === 'button' || field.type === 'checkbox' ? '' : `<label>${field.label}</label>`;
        const descHtml = field.type === 'checkbox' ? '' : `<div class="config-desc">${field.description}</div>`;

        return `<div class="config-item ${isDisabled ? 'disabled' : ''}">${labelHtml}${inputHtml}${descHtml}</div>`;
    }

    onConfigFieldChange(key, value) {
        this.updateDependentFields();
    }

    groupConfigByCategory() {
        const cats = {};
        this.configFields.forEach(f => {
            const cat = f.category || '其他设置';
            if (!cats[cat]) cats[cat] = [];
            cats[cat].push(f);
        });
        return cats;
    }

    getFieldOptions(field) {
        if (typeof field.options === 'string') {
            switch (field.options) {
                case 'availableModes': return pathConfig.getAvailableModes().map(m => ({ value: m, label: m }));
                case 'availableFps': return this.availableFps.map(f => ({ value: f, label: f === -1 ? '默认' : `${f} FPS` }));
                default: return [];
            }
        }
        return field.options || [];
    }

    isFieldDisabled(field, config) {
        if (!field.dependsOn) return false;
        const { field: depField, condition } = field.dependsOn;
        if (config[depField] === undefined) return false;
        return config[depField] !== condition;
    }

    bindFieldDependencies() {
        const form = document.getElementById('configForm');
        if (!form) return;
        this.configFields.forEach(field => {
            if (field.affects?.length > 0) {
                const input = form.querySelector(`[name="${field.key}"]`);
                if (input) input.addEventListener('change', () => this.updateDependentFields());
            }
            if (field.type === 'button') {
                const btn = form.querySelector(`button[data-key="${field.key}"]`);
                if (btn) btn.addEventListener('click', (e) => this.handleConfigButtonClick(field, e));
            }
        });
    }

    updateDependentFields() {
        const config = this.getCurrentConfigFormData();
        this.configFields.forEach(field => {
            if (field.dependsOn) {
                const dis = this.isFieldDisabled(field, config);
                // 原生 input 元素
                const input = document.querySelector(`[name="${field.key}"]`);
                const container = input?.closest('.config-item');
                if (input && container) {
                    input.disabled = dis;
                    container.classList.toggle('disabled', dis);
                }
                // 自定义下拉框
                const ddContainer = document.querySelector(`[data-dropdown-key="${field.key}"]`);
                if (ddContainer) {
                    const configItem = ddContainer.closest('.config-item');
                    if (configItem) configItem.classList.toggle('disabled', dis);
                }
            }
        });
    }

    async handleConfigButtonClick(field, event) {
        const button = event.target;
        const key = field.key;
        const needConfirm = field.require_confirmation !== false;

        if (button.classList.contains('confirming')) {
            this.cancelConfirmation(button);
            try {
                const result = await this.socketClient.communicate({ target: 'command', mode: 'write', data: [key] });
                if (result?.message?.trim()) this.showToast(result.message);
            } catch (error) {
                this.showToast('Error: ' + error.message);
            }
            return;
        }

        if (needConfirm) {
            button._origText = button.textContent;
            button._origClass = button.className;
            button.classList.remove('btn-ghost');
            button.classList.add('btn-accent', 'confirming');
            button.textContent = '确认？';
            button._confirmTimeout = setTimeout(() => this.cancelConfirmation(button), 5000);
        } else {
            try {
                const result = await this.socketClient.communicate({ target: 'command', mode: 'write', data: [key] });
                if (result?.message?.trim()) this.showToast(result.message);
            } catch (error) {
                this.showToast('Error: ' + error.message);
            }
        }
    }

    cancelConfirmation(button) {
        if (button._origClass) button.className = button._origClass;
        if (button._origText) button.textContent = button._origText;
        button.classList.remove('confirming');
        if (button._confirmTimeout) { clearTimeout(button._confirmTimeout); button._confirmTimeout = null; }
    }

    getCurrentConfigFormData() {
        const form = document.getElementById('configForm');
        const inputs = form.querySelectorAll('input:not([type="button"])');
        const config = {};
        inputs.forEach(input => {
            if (input.type === 'checkbox') config[input.name] = input.checked;
            else if (input.type === 'number') config[input.name] = parseInt(input.value) || 0;
            else config[input.name] = input.value;
        });
        // 从自定义下拉框获取值
        if (this.configDropdowns) {
            Object.keys(this.configDropdowns).forEach(key => {
                const dd = this.configDropdowns[key];
                if (dd) config[key] = dd.getValue();
            });
        }
        return config;
    }

    // ========== Toast ==========
    async showToast(message) {
        try {
            const { toast } = await import('./ksu.js');
            if (typeof ksu !== 'undefined') toast(message);
            else alert(message);
        } catch { alert(message); }
    }
}
