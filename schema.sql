-- STM32 三块开发板环境采集数据库
-- 服务启动时会自动创建数据库和表；本脚本用于手工初始化或检查结构。
CREATE DATABASE IF NOT EXISTS stm32_lab5
  CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;
USE stm32_lab5;

CREATE TABLE IF NOT EXISTS sensor_samples (
    id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT PRIMARY KEY,
    number BIGINT UNSIGNED NOT NULL COMMENT 'PC 端接收序号',
    board_id TINYINT UNSIGNED NOT NULL COMMENT '1/2/3 号开发板',
    session_id CHAR(32) NOT NULL COMMENT '开发板一次 TCP 连接会话',
    sequence BIGINT UNSIGNED NOT NULL COMMENT '开发板采样序号',
    uptime_ms BIGINT UNSIGNED NOT NULL COMMENT '开发板运行毫秒',
    collected_at DATETIME(6) NOT NULL COMMENT '按开发板时基换算的采集时间(UTC)',
    received_at DATETIME(6) NOT NULL COMMENT 'PC 接收时间(UTC)',
    temperature DECIMAL(6,2) NULL COMMENT '摄氏度',
    humidity DECIMAL(5,2) NULL COMMENT '相对湿度 %RH',
    light_adc SMALLINT UNSIGNED NULL COMMENT '光敏 ADC 0..4095',
    light_percent DECIMAL(5,2) NULL COMMENT '相对亮度 %',
    co2_ppm SMALLINT UNSIGNED NULL COMMENT 'CO2 ppm',
    formaldehyde_mg_m3 DECIMAL(6,2) NULL COMMENT '甲醛 mg/m3',
    valid TINYINT UNSIGNED NOT NULL COMMENT 'bit0 温湿度 bit1 光敏 bit2 CO2 bit3 甲醛',
    sharing BOOLEAN NOT NULL COMMENT '板 1 是否向板 2/3 共享',
    UNIQUE KEY sample_identity (board_id, session_id, sequence),
    KEY board_time (board_id, collected_at),
    KEY collection_time (collected_at)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;
