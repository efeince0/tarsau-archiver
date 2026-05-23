# ============================================================
# Makefile  —  tarsau Sistem Programlama Arşivleme Aracı
# ============================================================
# Kullanım:
#   make          → Programı derle
#   make clean    → Derleme çıktılarını temizle
#   make test     → Temel entegrasyon testlerini çalıştır
#   make check    → Valgrind ile bellek sızıntısı testi
# ============================================================

CC      = gcc
CFLAGS  = -Wall -Wextra -Wpedantic -std=c99 \
          -D_POSIX_C_SOURCE=200809L \
          -g

TARGET  = tarsau
SRCS    = tarsau.c
OBJS    = $(SRCS:.c=.o)

# Test dizini ve dosyaları
TEST_DIR   = test_cikti
ARCH_FILE  = test_arsiv.sau

# ---- Varsayılan Hedef ----------------------------------------
.PHONY: all
all: $(TARGET)
	@echo ">>> Derleme tamamlandı: ./$(TARGET)"

# ---- Bağlama (Link) ------------------------------------------
$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^
	@echo ">>> Bağlama başarılı: $@"

# ---- Derleme (Compile) ----------------------------------------
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# ---- Temizleme ------------------------------------------------
.PHONY: clean
clean:
	@echo ">>> Temizleniyor..."
	rm -f $(OBJS) $(TARGET)
	rm -f $(ARCH_FILE) a.sau
	rm -rf $(TEST_DIR) test_dosyalari
	@echo ">>> Temizleme tamamlandı."

# ---- Entegrasyon Testleri ------------------------------------
.PHONY: test
test: $(TARGET)
	@echo ""
	@echo "======================================================"
	@echo " TARSAU Entegrasyon Testleri"
	@echo "======================================================"

	@# --- Test dosyalarını oluştur ---
	@mkdir -p test_dosyalari
	@echo "Merhaba, bu birinci test dosyasıdır."  > test_dosyalari/t1.txt
	@echo "İkinci satır: A B C D E"               >> test_dosyalari/t1.txt
	@echo "Linux sistem programlama dersi."        > test_dosyalari/t2.txt
	@printf "Satir1\nSatir2\nSatir3\n"             > test_dosyalari/t3.txt
	@chmod 644 test_dosyalari/t1.txt
	@chmod 755 test_dosyalari/t2.txt
	@chmod 600 test_dosyalari/t3.txt
	@echo ">>> Test dosyaları oluşturuldu (t1.txt=644, t2.txt=755, t3.txt=600)"

	@echo ""
	@echo "--- TEST 1: Arşivleme (-b) ---"
	./$(TARGET) -b test_dosyalari/t1.txt \
	               test_dosyalari/t2.txt \
	               test_dosyalari/t3.txt \
	            -o $(ARCH_FILE)

	@echo ""
	@echo "--- TEST 2: Arşiv içeriğini incele (hex başlık) ---"
	@echo "İlk 80 karakter:"
	@head -c 80 $(ARCH_FILE); echo ""

	@echo ""
	@echo "--- TEST 3: Çıkarma (-a) belirtilen dizine ---"
	./$(TARGET) -a $(ARCH_FILE) $(TEST_DIR)

	@echo ""
	@echo "--- TEST 4: İzin doğrulama ---"
	@echo "Çıkarılan dosya izinleri:"
	@ls -l $(TEST_DIR)/
	@echo ""
	@echo "t1.txt beklenen: 644, gerçek:"
	@stat -c "%a" $(TEST_DIR)/t1.txt
	@echo "t2.txt beklenen: 755, gerçek:"
	@stat -c "%a" $(TEST_DIR)/t2.txt
	@echo "t3.txt beklenen: 600, gerçek:"
	@stat -c "%a" $(TEST_DIR)/t3.txt

	@echo ""
	@echo "--- TEST 5: Çıkarma (-a) varsayılan dizine ---"
	./$(TARGET) -a $(ARCH_FILE)
	@echo "Geçerli dizine çıkarılan dosyalar:"
	@ls -l t1.txt t2.txt t3.txt 2>/dev/null && rm -f t1.txt t2.txt t3.txt || true

	@echo ""
	@echo "--- TEST 6: Hatalı dosya (binary) reddedilmeli ---"
	@cp /bin/ls test_dosyalari/binary_dosya 2>/dev/null || true
	-./$(TARGET) -b test_dosyalari/binary_dosya -o hata_test.sau
	@rm -f hata_test.sau

	@echo ""
	@echo "--- TEST 7: Bozuk arşiv hata mesajı ---"
	@echo "bozuk_icerik_123" > bozuk.sau
	-./$(TARGET) -a bozuk.sau $(TEST_DIR)
	@rm -f bozuk.sau

	@echo ""
	@echo "--- TEST 8: Varsayılan çıktı adı (a.sau) ---"
	./$(TARGET) -b test_dosyalari/t1.txt
	@ls -lh a.sau
	@rm -f a.sau

	@echo ""
	@echo "======================================================"
	@echo " Tüm testler tamamlandı."
	@echo "======================================================"
	@rm -rf test_dosyalari $(TEST_DIR) $(ARCH_FILE)

# ---- Valgrind Bellek Testi ------------------------------------
.PHONY: check
check: $(TARGET)
	@which valgrind > /dev/null 2>&1 || \
		(echo "valgrind bulunamadı; yüklemek için: sudo apt install valgrind" && exit 1)
	@echo ">>> Valgrind bellek sızıntısı testi başlatılıyor..."
	@echo "Merhaba valgrind testi" > _vg_t1.txt
	@echo "İkinci satır"          > _vg_t2.txt
	valgrind --leak-check=full \
	         --error-exitcode=1 \
	         ./$(TARGET) -b _vg_t1.txt _vg_t2.txt -o _vg_test.sau
	valgrind --leak-check=full \
	         --error-exitcode=1 \
	         ./$(TARGET) -a _vg_test.sau _vg_out/
	@rm -f _vg_t1.txt _vg_t2.txt _vg_test.sau
	@rm -rf _vg_out
	@echo ">>> Valgrind testi başarılı: bellek sızıntısı yok."

# ---- Yardım ---------------------------------------------------
.PHONY: help
help:
	@echo ""
	@echo "Kullanılabilir make hedefleri:"
	@echo "  make          → Programı derle"
	@echo "  make clean    → Ara dosyaları ve çıktıları temizle"
	@echo "  make test     → Temel entegrasyon testlerini çalıştır"
	@echo "  make check    → Valgrind ile bellek testi yap"
	@echo "  make help     → Bu yardım mesajını göster"
	@echo ""
