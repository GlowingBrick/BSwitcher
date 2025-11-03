# 迁移 BSwitcher 配置文件
migrate_bswitcher_config() {
    # 检查并复制 config.json
    if [ -f "/data/adb/modules/BSwitcher/config.json" ]; then
        ui_print "尝试迁移配置"
        cp -f "/data/adb/modules/BSwitcher/config.json" "$MODPATH/"
    fi
    
    # 检查并复制 scheduler_config.json
    if [ -f "/data/adb/modules/BSwitcher/scheduler_config.json" ]; then
        ui_print "尝试迁移应用列表"
        cp -f "/data/adb/modules/BSwitcher/scheduler_config.json" "$MODPATH/"
    fi
}

migrate_bswitcher_config