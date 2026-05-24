/**
 * LD Radar Monitor - 配置管理
 */

// 默认配置
const DEFAULT_CONFIG = {
    system: {
        device_name: 'RadarMonitor-01',
        wifi_mode: 'ap',
        wifi_ssid: 'RadarMonitor',
        wifi_password: '12345678',
        wifi_channel: 6
    },
    
    radar: {
        type: 'LD2460',
        uart_num: 1,
        baud_rate: 115200,
        tx_pin: 1,
        rx_pin: 2,
        mount_mode: 'side',
        room: {
            width: 6.0,
            depth: 8.0,
            height: 3.0
        },
        position: {
            x: 0.0,
            y: 0.0,
            z: 1.5,
            angle: 0
        }
    },
    
    display: {
        canvas_width: 800,
        canvas_height: 600,
        grid_size: 0.5,
        show_grid: true,
        show_trail: true,
        trail_length: 60,
        show_radar_range: true,
        show_radar_position: true,
        target_colors: ['#FF4444', '#44FF44', '#4444FF'],
        trail_fade: true,
        trail_width: 2,
        target_size: 8
    },
    
    websocket: {
        port: 80,
        update_rate: 10,
        compression: false,
        heartbeat_interval: 30
    }
};

/**
 * 配置管理器
 */
class ConfigManager {
    constructor() {
        this.config = deepClone(DEFAULT_CONFIG);
        this.listeners = [];
        this.load();
    }
    
    /**
     * 获取配置值
     */
    get(path, defaultValue = null) {
        const keys = path.split('.');
        let value = this.config;
        
        for (const key of keys) {
            if (value === null || value === undefined) {
                return defaultValue;
            }
            value = value[key];
        }
        
        return value !== undefined ? value : defaultValue;
    }
    
    /**
     * 设置配置值
     */
    set(path, value) {
        const keys = path.split('.');
        let target = this.config;
        
        for (let i = 0; i < keys.length - 1; i++) {
            if (!(keys[i] in target)) {
                target[keys[i]] = {};
            }
            target = target[keys[i]];
        }
        
        const oldValue = target[keys[keys.length - 1]];
        target[keys[keys.length - 1]] = value;
        
        // 触发变更事件
        this.notify(path, value, oldValue);
        
        // 保存到本地存储
        this.save();
        
        return true;
    }
    
    /**
     * 批量设置配置
     */
    setMultiple(updates) {
        for (const [path, value] of Object.entries(updates)) {
            this.set(path, value);
        }
    }
    
    /**
     * 获取完整配置
     */
    getAll() {
        return deepClone(this.config);
    }
    
    /**
     * 重置为默认配置
     */
    reset() {
        this.config = deepClone(DEFAULT_CONFIG);
        this.notify('config', this.config, null);
        this.save();
    }
    
    /**
     * 加载配置
     */
    load() {
        const saved = Storage.get('radar_monitor_config');
        if (saved) {
            this.config = this.mergeDeep(deepClone(DEFAULT_CONFIG), saved);
            Logger.info('Config loaded from storage');
        }
    }
    
    /**
     * 保存配置
     */
    save() {
        Storage.set('radar_monitor_config', this.config);
    }
    
    /**
     * 订阅配置变更
     */
    subscribe(callback) {
        this.listeners.push(callback);
        return () => {
            const index = this.listeners.indexOf(callback);
            if (index > -1) {
                this.listeners.splice(index, 1);
            }
        };
    }
    
    /**
     * 通知所有监听器
     */
    notify(path, newValue, oldValue) {
        this.listeners.forEach(callback => {
            try {
                callback(path, newValue, oldValue);
            } catch (e) {
                Logger.error('Config listener error:', e);
            }
        });
    }
    
    /**
     * 深度合并对象
     */
    mergeDeep(target, source) {
        const output = Object.assign({}, target);
        if (this.isObject(target) && this.isObject(source)) {
            Object.keys(source).forEach(key => {
                if (this.isObject(source[key])) {
                    if (!(key in target)) {
                        Object.assign(output, { [key]: source[key] });
                    } else {
                        output[key] = this.mergeDeep(target[key], source[key]);
                    }
                } else {
                    Object.assign(output, { [key]: source[key] });
                }
            });
        }
        return output;
    }
    
    /**
     * 检查是否为对象
     */
    isObject(item) {
        return item && typeof item === 'object' && !Array.isArray(item);
    }
}

// 创建全局配置实例
const config = new ConfigManager();
