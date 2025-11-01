// UI管理器
import { InfoManager } from './infoManager.js';
import { pathConfig } from './pathConfig.js';

export class UIManager {
    constructor(configManager, appLoader) {
        this.configManager = configManager;
        this.appLoader = appLoader;
        this.infoManager = new InfoManager();
        
        this.modal = document.getElementById('addRuleModal');
        this.configModal = document.getElementById('configModal');
        this.allApps = [];
        this.refreshBtn = null;
        this.rulesList = null;
        this.pendingDeleteRuleId = null;
        this.deleteTimeout = null;
        
        // 增强的配置字段定义
        this.configFields = [];
        
        // 按分类分组
        this.configCategories = this.groupConfigByCategory();
    }

    // 初始化UI
    async init() {
        await this.loadConfigFields();
        await this.loadSystemInfo();
        this.bindEvents();
        this.renderDefaultMode();
        this.renderRulesList();
        this.populateModeSelects();
        this.allApps = this.appLoader.getAvailableApps();
        this.populateAppSelect();
    }

    // 加载配置字段定义
    async loadConfigFields() {
        try {
            await this.infoManager.loadConfigFields();
            this.configFields = this.infoManager.getConfigFields();
            this.configCategories = this.groupConfigByCategory();
            console.log('配置字段加载完成:', this.configFields);
        } catch (error) {
            console.error('加载配置字段失败:', error);
            this.configFields = [];
            this.configCategories = {};
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

        // 默认模式变更
        document.getElementById('defaultMode').addEventListener('change', (e) => {
            this.configManager.updateDefaultMode(e.target.value);
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

        // 关键修复：为规则列表容器绑定事件委托
        this.rulesList = document.getElementById('rulesList');
        if (this.rulesList) {
            this.rulesList.addEventListener('click', (e) => {
                // 检查点击的是否是删除按钮
                if (e.target.classList.contains('delete-rule')) {
                    const ruleId = e.target.getAttribute('data-rule-id');
                    if (ruleId) {
                        this.handleDeleteClick(ruleId, e.target);
                    }
                }
            });
        }
    }

    // 修改 handleDeleteClick 方法，使用包名
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


    // 修改取消删除方法
    cancelDelete(button, originalText, originalClass) {
        if (button) {
            button.textContent = originalText || '删除';
            button.className = originalClass || 'btn btn-danger delete-rule';
        }
        this.pendingDeleteAppPackage = null;
        this.deleteTimeout = null;
    }



    // 修改 executeDelete 方法，使用包名
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
            console.error('删除规则失败:', error);
            this.showToast('删除规则失败: ' + error.message);
        } finally {
            this.pendingDeleteAppPackage = null;
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
            console.error('刷新应用列表失败:', error);

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
    showModal() {
        if (this.modal) {
            this.modal.style.display = 'block';
            // 重置表单
            const appSearch = document.getElementById('appSearch');
            const appSelect = document.getElementById('appSelect');
            const modeSelect = document.getElementById('modeSelect');

            if (appSearch) appSearch.value = '';
            if (appSelect) appSelect.value = '';
            if (modeSelect) modeSelect.value = this.configManager.getConfig().defaultMode;

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

        if (!defaultModeSelect || !modeSelect) return;

        const availableModes = this.configManager.getAvailableModes();

        [defaultModeSelect, modeSelect].forEach(select => {
            select.innerHTML = '';
            availableModes.forEach(mode => {
                const option = document.createElement('option');
                option.value = mode;
                option.textContent = mode;
                select.appendChild(option);
            });
        });

        // 设置默认模式当前值
        const config = this.configManager.getConfig();
        defaultModeSelect.value = config.defaultMode;
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
            
            return `
                <div class="rule-item">
                    <div class="rule-info">
                        <div class="rule-name" title="${appName}">${appName}</div>
                        <div class="rule-details" title="包名: ${rule.appPackage} | 模式: ${modeName}">
                            包名: ${rule.appPackage} | 模式: ${modeName}
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

        if (!appSelect || !modeSelect) return;

        const appPackage = appSelect.value;
        const mode = modeSelect.value;

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
            await this.configManager.addRule(appPackage, mode);
            this.renderRulesList();
            this.hideModal();
        } catch (error) {
            console.error('保存规则失败:', error);
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
                // 在网页环境中使用alert作为备用
                alert(message);
            }
        } catch (error) {
            console.log('Toast显示失败:', error);
            alert(message); // 最终备用方案
        }
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
                // 可以在这里添加其他动态选项类型
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
        const dependentValue = currentConfig[dependentField];
        
        // 如果依赖字段的值不等于条件值，则禁用此字段
        return dependentValue !== condition;
    }

    // 获取受影响的字段
    getAffectedFields(fieldKey) {
        return this.configFields.filter(field => 
            field.affects && field.affects.includes(fieldKey)
        );
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

    // 获取当前表单数据（不保存）
    getCurrentConfigFormData() {
        const form = document.getElementById('configForm');
        const inputs = form.querySelectorAll('input, select');
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


    // 加载系统信息
    async loadSystemInfo() {
        try {
            await this.infoManager.loadSystemInfo();
            this.renderSystemInfo();
        } catch (error) {
            console.error('加载系统信息失败:', error);
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
        // 现有的事件绑定...
        document.getElementById('addRuleBtn').addEventListener('click', () => {
            this.showModal();
        });

        document.querySelector('.close').addEventListener('click', () => {
            this.hideModal();
        });

        document.getElementById('cancelBtn').addEventListener('click', () => {
            this.hideModal();
        });

        document.getElementById('saveRuleBtn').addEventListener('click', () => {
            this.saveRule();
        });

        document.getElementById('defaultMode').addEventListener('change', (e) => {
            this.configManager.updateDefaultMode(e.target.value);
        });

        this.modal.addEventListener('click', (e) => {
            if (e.target === this.modal) {
                this.hideModal();
            }
        });

        const appSearch = document.getElementById('appSearch');
        if (appSearch) {
            appSearch.addEventListener('input', (e) => {
                this.searchApps(e.target.value);
            });
        }

        this.refreshBtn = document.getElementById('refreshAppListBtn');
        if (this.refreshBtn) {
            this.refreshBtn.addEventListener('click', () => {
                this.refreshAppList();
            });
        }

        this.rulesList = document.getElementById('rulesList');
        if (this.rulesList) {
            this.rulesList.addEventListener('click', (e) => {
                if (e.target.classList.contains('delete-rule')) {
                    const appPackage = e.target.getAttribute('data-app-package');
                    if (appPackage) {
                        this.handleDeleteClick(appPackage, e.target);
                    }
                }
            });
        }

        // 新增配置页面事件
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

        // 场景模式变化时更新mode_file的可用状态
        if (this.configModal) {
            this.configModal.addEventListener('change', (e) => {
                if (e.target.name === 'scene') {
                    this.updateModeFileAvailability(e.target.checked);
                }
            });
        }
    }


    // 显示配置模态框
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

    // 获取配置表单数据（用于保存）
    getConfigFormData() {
        return this.getCurrentConfigFormData();
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
                        <option value="">请选择</option>
                        ${optionHtml}
                    </select>
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
                <label>${field.label}</label>
                ${fieldHtml}
                <div class="config-description">${field.description}</div>
            </div>
        `;
    }


    // 更新mode_file字段的可用状态
    updateModeFileAvailability(sceneEnabled) {
        const modeFileItem = document.querySelector('.config-item input[name="mode_file"]');
        const modeFileContainer = document.querySelector('.config-item input[name="mode_file"]')?.closest('.config-item');
        
        if (modeFileItem && modeFileContainer) {
            if (sceneEnabled) {
                modeFileItem.disabled = true;
                modeFileContainer.classList.add('disabled');
            } else {
                modeFileItem.disabled = false;
                modeFileContainer.classList.remove('disabled');
            }
        }
    }


}
