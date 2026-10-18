# EDPC - EgEhE's Dynamic Pantograph Configurator ⚡

**EDPC** (EgEhE's Dynamic Pantograph Configurator), Minecraft Transit Railway (MTR) modundaki vagonlara dinamik, kataner takip eden ve 4-bar bağlantılı pantografları kolayca yapılandırmak, 3D olarak Vulkan ile gerçek zamanlı simüle etmek ve doğrudan tren paketine paketlemek için geliştirilmiş Qt6 + Rust + Vulkan tabanlı modern masaüstü uygulamasıdır.

---

## 🚀 Özellikler

1. **3D Vulkan Gerçek Zamanlı Önizleme**:
   - `QVulkanWindow` ve yerel Vulkan 1.4 grafik hattı.
   - OBJ pantograf modeli (`p_base`, `p1`, `p2`, `p3`, `p4`) yükleme ve görselleştirme.
   - Üst kataner referans teli ve çatı seviyesi grid ızgarası.
   - Fare ile 360° serbest yörünge kamera kontrolleri (Sol tık: Döndür, Shift+Sol / Orta tık: Kaydır, Tekerlek: Yakınlaş).

2. **Kinematik & Yükseklik Parametreleri**:
   - `WIRE_TRACKING_REFERENCE` (Referans kataner yüksekliği)
   - `NO_WIRE_PARK_HEIGHT` (Telsiz park yüksekliği)
   - `DEPOT_PARK_HEIGHT` & `ENABLE_DEPOT_PARK` (Depo park modu)
   - `pantoUpperTreshold` & `pantoLowerTreshold` (Kataner algılama sınırları)
   - Canlı test kaydırıcısı ile alt kol, üst kol ve 4-bar çubuk açılarının anlık hesaplanması.
   - Düşük yüksekliklerdeki orantısızlaşmayı önleyen **Adaptif Z-Telafi** motoru.

3. **Tren Paketi İnceleme & Vagon Montaj Ayarları**:
   - `.zip` arşivleri veya doğrudan çıkarılmış klasör paketlerini tarama.
   - `assets/mtr/mtr_custom_resources.json` dosyasını otomatik okuyarak paketteki tüm araçları listeleme.
   - Hangi vagonlara pantograf takılacağını onay kutularıyla belirleme.
   - Araç bazlı montaj ayarları: `mountHeight`, `mountX`, `mountZ`, `rotationYDeg` (0°/180°), `wireDetectZ`.
   - Hızlı konum önayarları (Ön Tavan +5.2m, Arka Tavan -5.2m 180°, Merkez 0.0m).

4. **Otomatik Paketleme & Rastgele Script Üretimi**:
   - Rastgele 8 haneli ID formatında script üretimi: `[random_8]_pantograph.js` (örn. `8cvjdycq_pantograph.js`).
   - `mtr_custom_resources.json` içine seçilen araçlar için `scriptFiles` / `scriptId` yollarının otomatik gömülmesi.
   - OBJ, MTL ve doku dosyalarının `assets/mtr/panto/` altına kopyalanması.
   - Minecraft'a doğrudan yüklemeye hazır yeni `.zip` çıktısı.

---

## 🛠️ Derleme ve Çalıştırma

### Gereksinimler:
- Linux / Windows
- Rust (Cargo)
- Qt 6 (Widgets, Gui, Core)
- Vulkan Sürücüsü (Intel / NVIDIA / AMD)
- CMake 3.20+ ve C++17 derleyici (GCC, Clang veya MSVC)

### Çalıştırma:
Uygulama zaten derlenmiş ve hazırdır:
```bash
# Terminalden çalıştırma:
/home/ege/EDPC/run_edpc.sh
# veya
edpc
```

### Yeniden Derlemek İstenirse:
```bash
cd /home/ege/EDPC
cargo build --release --manifest-path edpc_core/Cargo.toml
cmake -B build -S .
cmake --build build
```

---

## 📄 Lisans & Telif Hakkı (License & Copyright)

Bu proje **GNU General Public License v3.0 (GPL-3.0)** ile korunmaktadır.

- **Orijinal Geliştirici:** **EgEhEw** (<egegokcuogli@hotmail.com>)
- **Kullanım ve Fork Kuralları:**
  - Yazılımı dilediğiniz gibi kullanabilir, inceleyebilir, değiştirebilir ve fork edebilirsiniz.
  - **Copyleft Zorunluluğu:** Yapılan her değişiklik ve türetilen her fork da **aynı şekilde açık kaynak (GPL-3.0)** kalmak zorundadır (kapalı kaynak / ticari olarak gizlenemez).
  - **Atıf Zorunluluğu:** Orijinal geliştirici (**EgEhEw**) bilgisi ve telif hakkı metni korunmalıdır.

