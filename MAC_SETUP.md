# Hướng Dẫn Cài Đặt Dự Án Geopix Trên Mac

## Tổng Quan Dự Án
Hệ thống trigger camera dựa trên ESP32-S3 với các tính năng:
- Auto Shoot mode (cảm biến LiDAR TF-Luna)
- Timelapse mode
- Trigger mode (đầu ra kép)
- Sleep Week Scheduler
- Cài đặt hiển thị với tiết kiệm pin
- Remote camera Bluetooth (Sony/Canon/Nikon)

## Hướng Dẫn Cài Đặt Trên Mac

### Yêu Cầu Cơ Bản

1. **Cài đặt Homebrew** (nếu chưa có)
   ```bash
   /bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"
   ```

2. **Cài đặt PlatformIO Core**
   ```bash
   brew install platformio
   ```

3. **Cài đặt VS Code** (IDE khuyến nghị)
   - Tải từ: https://code.visualstudio.com/
   - Cài đặt extension PlatformIO IDE

4. **Cài đặt Python 3** (nếu cần)
   ```bash
   brew install python@3
   ```

5. **Cài đặt USB drivers cho ESP32-S3**
   ```bash
   brew install --cask ch340-driver  # Cho USB-to-serial CH340
   # Hoặc drivers CP210x từ website Silicon Labs
   ```

## Thiết Lập Dự Án Trên Mac

1. **Copy folder dự án sang Mac**
   - Copy folder `Geopix_Project_PORTABLE` sang Mac của bạn

2. **Mở trong VS Code**
   ```bash
   cd /path/to/Geopix_Project_PORTABLE
   code .
   ```

3. **Cài đặt dependencies**
   ```bash
   pio install
   ```

4. **Build dự án**
   ```bash
   pio run
   ```

5. **Upload lên ESP32-S3**
   ```bash
   # Kết nối ESP32-S3 qua USB
   pio run --target upload
   ```

6. **Theo dõi serial output**
   ```bash
   pio device monitor
   ```

## Cấu Hình PlatformIO

File `platformio.ini` đã được cấu hình sẵn cho ESP32-S3:
- Platform: espressif32@6.3.1
- Board: esp32-s3-devkitc-1
- Framework: Arduino
- CPU: 240MHz

Không cần thay đổi gì để tương thích với Mac.

## Lưu Ý Riêng Cho Mac

### Phát Hiện Cổng USB
- Trên Mac, cổng USB thường là `/dev/cu.usbserial-*` hoặc `/dev/tty.usbserial-*`
- PlatformIO tự động phát hiện cổng, nhưng bạn có thể chỉ định thủ công:
  ```bash
  pio run --target upload --upload-port /dev/cu.usbserial-XXXX
  ```

### Quyền Truy Cập
- Có thể cần cấp quyền cho VS Code truy cập thiết bị USB
- Vào: System Preferences > Security & Privacy > Privacy > Files and Folders

### Build Artifacts
- File build nằm trong `.pio/build/` (không được bao gồm trong dự án)
- Để clean build: `pio run --target clean`

### Xử Lý Vấn Đề

**Không tìm thấy cổng:**
```bash
# Liệt kê các cổng có sẵn
pio device list
```

**Bị từ chối quyền truy cập:**
```bash
sudo chmod 666 /dev/cu.usbserial-*
```

**Vấn đề Python:**
```bash
# Đảm bảo dùng Python 3
python3 --version
# Cài lại PlatformIO
pip3 install -U platformio
```

## Cấu Trúc Dự Án

```
Geopix_Project_PORTABLE/
├── src/              # Source code
│   ├── auto_shoot/   # Auto Shoot mode
│   ├── timelapse/    # Timelapse mode
│   ├── trigger_mode/ # Trigger mode
│   ├── sleep_week/   # Sleep scheduler
│   ├── display_mode/ # Display settings
│   ├── setting/      # System settings
│   ├── remote/       # BLE camera drivers
│   └── main.cpp      # Main entry point
├── include/          # Header files
├── lib/              # Libraries (bao gồm SmoothUIToolKit local)
├── test/             # Tests
├── platformio.ini    # PlatformIO config
└── MAC_SETUP.md      # File này
```

## Quy Trình Phát Triển

1. Thay đổi code
2. Build: `pio run`
3. Upload: `pio run --target upload`
4. Monitor: `pio device monitor`

## Hỗ Trợ

Tài liệu PlatformIO: https://docs.platformio.org

Tài liệu ESP32-S3: https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/
