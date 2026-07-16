/**
 * 自定义下拉框组件
 * 替换原生 <select> 元素，支持深色/浅色主题
 * 下拉菜单使用 fixed 定位，避免被父容器 overflow 裁剪
 */

export class CustomDropdown {
    constructor(container, options = {}) {
        this.container = container;
        this.items = options.items || [];
        this.placeholder = options.placeholder || '请选择';
        this.onChange = options.onChange || (() => {});
        this.searchable = options.searchable || false;
        this.searchPlaceholder = options.searchPlaceholder || '搜索...';
        this.isOpen = false;
        this.filteredItems = [...this.items];
        this.menuEl = null;
        this.triggerEl = null;

        // 统一将 value 转为 string，避免 number/string 比较不一致
        this._value = this._coerceValue(options.value ?? '');

        this.render();
        this.bindEvents();
    }

    _coerceValue(v) {
        return v === null || v === undefined ? '' : String(v);
    }

    _findItem(value) {
        const sv = this._coerceValue(value);
        return this.items.find(i => this._coerceValue(i.value) === sv);
    }

    render() {
        const item = this._findItem(this._value);

        this.container.innerHTML = '';
        this.triggerEl = document.createElement('div');
        this.triggerEl.className = 'dropdown-trigger';
        this.triggerEl.tabIndex = 0;
        this.triggerEl.innerHTML = `
            <span class="selected-text ${item ? '' : 'placeholder'}">${item ? item.label : this.placeholder}</span>
            <span class="arrow">▼</span>
        `;
        this.container.appendChild(this.triggerEl);

        this.menuEl = document.createElement('div');
        this.menuEl.className = 'dropdown-menu';
        this.menuEl.innerHTML = `
            ${this.searchable ? `<div class="dropdown-search"><input type="text" placeholder="${this.searchPlaceholder}"></div>` : ''}
            <div class="dropdown-items"></div>
            <div class="dropdown-hint"></div>
        `;
        document.body.appendChild(this.menuEl);

        this.itemsContainer = this.menuEl.querySelector('.dropdown-items');
        this.hint = this.menuEl.querySelector('.dropdown-hint');

        this.renderItems();
    }

    renderItems(filter = '') {
        const lf = filter.toLowerCase();
        this.filteredItems = filter
            ? this.items.filter(i => i.label.toLowerCase().includes(lf) || String(i.value).toLowerCase().includes(lf))
            : [...this.items];

        this.itemsContainer.innerHTML = this.filteredItems.map(item => `
            <div class="dropdown-item ${this._coerceValue(item.value) === this._value ? 'selected' : ''}" data-value="${item.value}">
                ${item.label}
            </div>
        `).join('');

        this.hint.textContent = `${this.filteredItems.length} / ${this.items.length} 项`;
    }

    bindEvents() {
        this.triggerEl.addEventListener('click', (e) => {
            e.stopPropagation();
            this.toggle();
        });

        this.itemsContainer.addEventListener('click', (e) => {
            const el = e.target.closest('.dropdown-item');
            if (el) this.select(el.dataset.value);
        });

        if (this.searchable) {
            const si = this.menuEl.querySelector('.dropdown-search input');
            if (si) {
                si.addEventListener('input', (e) => this.renderItems(e.target.value));
                si.addEventListener('click', (e) => e.stopPropagation());
            }
        }

        this.triggerEl.addEventListener('keydown', (e) => {
            if (e.key === 'Enter' || e.key === ' ') { e.preventDefault(); this.toggle(); }
            else if (e.key === 'Escape') this.close();
        });

        document.addEventListener('click', () => this.close());
        document.addEventListener('scroll', () => this.close(), true);

        // 焦点离开整个下拉框时关闭（延迟检测，避免点搜索框自身触发）
        this._onBlur = () => {
            setTimeout(() => {
                if (!this.menuEl.contains(document.activeElement) && !this.triggerEl.contains(document.activeElement)) {
                    this.close();
                }
            }, 150);
        };
        this.triggerEl.addEventListener('blur', this._onBlur);
        this.menuEl.addEventListener('blur', this._onBlur);
    }

    toggle() { this.isOpen ? this.close() : this.open(); }

    open() {
        this.closeAllDropdowns();
        this.isOpen = true;
        this.triggerEl.classList.add('open');
        this.menuEl.classList.add('open');
        this.positionMenu();

        if (this.searchable) {
            const si = this.menuEl.querySelector('.dropdown-search input');
            if (si) { si.value = ''; this.renderItems(''); setTimeout(() => si.focus(), 50); }
        }
    }

    positionMenu() {
        const rect = this.triggerEl.getBoundingClientRect();
        const vh = window.innerHeight;
        const vw = window.innerWidth;

        this.menuEl.style.position = 'fixed';
        this.menuEl.style.left = rect.left + 'px';
        this.menuEl.style.width = rect.width + 'px';
        this.menuEl.style.zIndex = '10000';
        this.menuEl.style.maxHeight = '';
        this.menuEl.style.top = '';
        this.menuEl.style.bottom = '';

        const spaceBelow = vh - rect.bottom;
        const spaceAbove = rect.top;

        // 先让菜单显示以测量真实高度
        this.menuEl.style.display = 'block';
        const realH = this.menuEl.scrollHeight;
        const itemsEl = this.menuEl.querySelector('.dropdown-items');

        if (spaceBelow >= realH) {
            // 下方空间充足，向下展开
            this.menuEl.style.top = rect.bottom + 'px';
        } else if (spaceAbove >= realH) {
            // 上方空间充足，向上展开
            this.menuEl.style.top = 'auto';
            this.menuEl.style.bottom = (vh - rect.top) + 'px';
        } else {
            // 上下都不够，向下展开并限制高度
            const maxH = Math.max(spaceBelow - 4, 80);
            this.menuEl.style.top = rect.bottom + 'px';
            this.menuEl.style.maxHeight = maxH + 'px';
            if (itemsEl) itemsEl.style.maxHeight = Math.max(maxH - 60, 40) + 'px';
        }

        if (rect.left + rect.width > vw) {
            this.menuEl.style.left = (vw - rect.width - 8) + 'px';
        }

        // 重置 itemsEl maxHeight（非截断情况不限制）
        if (!this.menuEl.style.maxHeight && itemsEl) {
            itemsEl.style.maxHeight = '';
        }
    }

    close() {
        if (!this.isOpen) return;
        this.isOpen = false;
        this.triggerEl.classList.remove('open');
        this.menuEl.classList.remove('open');
        this.menuEl.style.cssText = '';
    }

    closeAllDropdowns() {
        document.querySelectorAll('.dropdown-menu.open').forEach(menu => {
            menu.classList.remove('open');
            menu.style.cssText = '';
        });
        document.querySelectorAll('.dropdown-trigger.open').forEach(t => t.classList.remove('open'));
    }

    select(value) {
        this._value = this._coerceValue(value);
        const item = this._findItem(value);
        const textEl = this.triggerEl.querySelector('.selected-text');
        textEl.textContent = item ? item.label : this.placeholder;
        textEl.classList.toggle('placeholder', !item);

        this.itemsContainer.querySelectorAll('.dropdown-item').forEach(el => {
            el.classList.toggle('selected', this._coerceValue(el.dataset.value) === this._value);
        });

        this.close();
        this.onChange(this._value);
    }

    setValue(value) {
        this._value = this._coerceValue(value);
        const item = this._findItem(value);
        const textEl = this.triggerEl.querySelector('.selected-text');
        textEl.textContent = item ? item.label : this.placeholder;
        textEl.classList.toggle('placeholder', !item);

        this.itemsContainer.querySelectorAll('.dropdown-item').forEach(el => {
            el.classList.toggle('selected', this._coerceValue(el.dataset.value) === this._value);
        });
    }

    updateItems(items) {
        this.items = items;
        this.renderItems();
        if (this._value && !this._findItem(this._value)) {
            this._value = '';
            const textEl = this.triggerEl.querySelector('.selected-text');
            textEl.textContent = this.placeholder;
            textEl.classList.add('placeholder');
        }
    }

    getValue() { return this._value; }

    destroy() {
        this.triggerEl.removeEventListener('blur', this._onBlur);
        this.menuEl.removeEventListener('blur', this._onBlur);
        if (this.menuEl?.parentNode) this.menuEl.parentNode.removeChild(this.menuEl);
        this.container.innerHTML = '';
    }
}
