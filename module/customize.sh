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

    # 检查并复制 static_data.json
    if [ -f "/data/adb/modules/BSwitcher/static_data.json" ]; then
        ui_print "尝试迁移静态数据"
        cp -f "/data/adb/modules/BSwitcher/static_data.json" "$MODPATH/"
    fi

    # 检查并复制 powerlog.json
    if [ -f "/data/adb/modules/BSwitcher/powerlog.json" ]; then
        ui_print "尝试迁移功耗记录"
        cp -f "/data/adb/modules/BSwitcher/powerlog.json" "$MODPATH/"
    fi
}

if /system/bin/nc --help 2>&1 | grep -q -e "-U"; then
rm $MODPATH/socket_send
else
ui_print "系统不支持nc -U,使用内置方案"
mkdir -p $MODPATH/system/bin
mv $MODPATH/socket_send $MODPATH/system/bin/socket_send
set_perm $MODPATH/system/bin/socket_send 0 0 0755
fi
set_perm $MODPATH/apt
set_perm $MODPATH/BSwitcher

migrate_bswitcher_config