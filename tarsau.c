/*
 * ============================================================
 * tarsau.c  —  Sistem Programlama Arşivleme Aracı
 * ============================================================
 * Linux/Unix ortamında çalışan, sıkıştırma yapmayan,
 * tar/zip benzeri bir arşivleme programı.
 *
 * Kullanım:
 *   Arşivleme : tarsau -b dosya1 dosya2 ... [-o cikti.sau]
 *   Çıkarma   : tarsau -a arsiv.sau [hedef_dizin]
 *
 * .sau Arşiv Dosyası Formatı:
 *   Bölüm 1 (Organizasyon):
 *     [0..9]  → 10 baytlık ASCII sayısal bölüm-1 boyutu  (örn. 0000000150)
 *     [10..]  → |dosyaadi,izinler,boyut| şeklinde kayıtlar
 *   Bölüm 2 (İçerik):
 *     Dosya içerikleri art arda, ayırıcısız ASCII metin
 * ============================================================
 */

#define _POSIX_C_SOURCE 200809L   /* POSIX.1-2008 sistem çağrıları için */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>       /* open()  */
#include <unistd.h>      /* read(), write(), close() */
#include <sys/stat.h>    /* stat(), chmod(), mkdir() */
#include <sys/types.h>
#include <errno.h>
#include <ctype.h>       /* isdigit() */

/* ===================== SABİT TANIMLAR ===================== */
#define MAX_DOSYA_SAYISI    32                       /* Maksimum giriş dosyası sayısı        */
#define MAX_TOPLAM_BOYUT    (200LL * 1024 * 1024)   /* 200 MB toplam boyut sınırı           */
#define BASLIK_ALANI_BOYU   10                       /* Bölüm-1 boyutunu tutan alan (bayt)   */
#define MAX_DOSYA_ADI       256                      /* Maksimum dosya adı uzunluğu          */
#define TAMPON_BOYU         8192                     /* G/Ç tampon boyutu                    */

/* =================== VERİ YAPILARI ======================== */

/* Arşiv içindeki her dosyaya ait meta-veri kaydı */
typedef struct {
    char   ad[MAX_DOSYA_ADI]; /* Dosya adı (veya yolu)      */
    mode_t izinler;            /* POSIX izin bitleri (octal) */
    off_t  boyut;              /* Dosya boyutu (bayt)        */
} DosyaKaydi;

/* =================== YARDIMCI FONKSİYONLAR ================ */

/*
 * metin_dosyasi_mi()
 * ------------------
 * Verilen dosyanın salt ASCII (7-bit) metin dosyası olup
 * olmadığını kontrol eder. Bir bayt dahi 0x7F'yi aşarsa
 * veya dosya açılamazsa 0 (hayır), aksi hâlde 1 (evet) döner.
 */
static int metin_dosyasi_mi(const char *dosya_yolu)
{
    int fd;
    unsigned char tampon[TAMPON_BOYU];
    ssize_t okunan;

    fd = open(dosya_yolu, O_RDONLY);
    if (fd < 0) return 0;

    while ((okunan = read(fd, tampon, sizeof(tampon))) > 0) {
        for (ssize_t i = 0; i < okunan; i++) {
            /* 0x80 (128) ve üzeri baytlar ASCII değildir */
            if (tampon[i] > 0x7F) {
                close(fd);
                return 0;
            }
        }
    }
    close(fd);
    return 1; /* Tüm baytlar ASCII aralığında */
}

/*
 * taban_ad()
 * ----------
 * Bir dosya yolundan sadece taban adını (basename) döndürür.
 * Örn: "dizin/alt/dosya.txt"  →  "dosya.txt"
 * Örn: "t1"                   →  "t1"
 */
static const char *taban_ad(const char *yol)
{
    const char *son_egik = strrchr(yol, '/');
    return son_egik ? son_egik + 1 : yol;
}

/*
 * dizin_olustur_veya_dogrula()
 * ----------------------------
 * Verilen yolda bir dizin yoksa oluşturur;
 * varsa dizin olup olmadığını doğrular.
 * Başarıda 0, hata durumunda -1 döner.
 */
static int dizin_olustur_veya_dogrula(const char *yol)
{
    struct stat durum;

    if (stat(yol, &durum) == 0) {
        /* Yol mevcut; dizin mi? */
        if (!S_ISDIR(durum.st_mode)) {
            fprintf(stderr,
                    "Hata: '%s' zaten var ancak bir dizin değil!\n", yol);
            return -1;
        }
        return 0; /* Dizin zaten mevcut */
    }

    /* stat() başarısız → dizin yok, oluştur */
#ifdef _WIN32
    if (mkdir(yol) < 0) {
#else
    if (mkdir(yol, 0755) < 0) {
#endif
        fprintf(stderr,
                "Hata: '%s' dizini oluşturulamadı: %s\n",
                yol, strerror(errno));
        return -1;
    }
    return 0;
}

/*
 * guvensiz_yaz()
 * --------------
 * Verilen tamponu dosya tanımlayıcısına eksiksiz yazar.
 * Kısmi yazma veya hata durumunda -1 döner.
 */
static int guvensiz_yaz(int fd, const void *tampon, size_t adet)
{
    const char *p   = (const char *)tampon;
    size_t     kalan = adet;

    while (kalan > 0) {
        ssize_t y = write(fd, p, kalan);
        if (y <= 0) return -1;
        p     += y;
        kalan -= (size_t)y;
    }
    return 0;
}

/* =================== ARŞİVLEME FONKSİYONLARI ============= */

/*
 * organizasyon_bolumu_olustur()
 * -----------------------------
 * DosyaKaydi dizisinden " |ad,izin,boyut| " formatında
 * organizasyon metnini dinamik bellekte oluşturur.
 * Çağıran taraf dönen işaretçiyi free() ile serbest bırakmalıdır.
 * org_uzunluk: oluşturulan metnin bayt sayısını alır.
 * Hata durumunda NULL döner.
 */
static char *organizasyon_bolumu_olustur(const DosyaKaydi *kayitlar,
                                         int sayi,
                                         int *org_uzunluk)
{
    /*
     * Her kayıt en fazla:
     *   1 (|) + MAX_DOSYA_ADI + 1 (,) + 4 (izin) + 1 (,) + 20 (boyut) + 1 (|)
     *   = MAX_DOSYA_ADI + 28 ≈ 284 karakter
     */
    int tampon_boyu = sayi * (MAX_DOSYA_ADI + 32);
    char *org = malloc((size_t)tampon_boyu);
    if (!org) {
        fprintf(stderr, "Hata: Bellek ayırma başarısız!\n");
        return NULL;
    }

    int toplam = 0;
    for (int i = 0; i < sayi; i++) {
        /* Format: |dosyaadi,0644,1024| */
        int n = snprintf(org + toplam, (size_t)(tampon_boyu - toplam),
                         "|%s,%04o,%lld|",
                         kayitlar[i].ad,
                         (unsigned int)kayitlar[i].izinler,
                         (long long)kayitlar[i].boyut);

        if (n < 0 || n >= tampon_boyu - toplam) {
            fprintf(stderr, "Hata: Organizasyon tamponu yetersiz!\n");
            free(org);
            return NULL;
        }
        toplam += n;
    }

    *org_uzunluk = toplam;
    return org;
}

/*
 * arsivle()
 * ---------
 * -b modunun ana fonksiyonu.
 * Belirtilen giriş dosyalarını doğrular, meta-verileri toplar
 * ve .sau formatında çıktı dosyasını oluşturur.
 *
 * dosyalar     : arşivlenecek dosya yolları dizisi
 * dosya_sayisi : dizinin uzunluğu
 * cikti_dosyasi: oluşturulacak .sau dosyasının adı
 *
 * Başarıda 0, hata durumunda -1 döner.
 */
static int arsivle(char *dosyalar[], int dosya_sayisi,
                   const char *cikti_dosyasi)
{
    DosyaKaydi  kayitlar[MAX_DOSYA_SAYISI];
    int         gecerli_sayi = 0;
    long long   toplam_boyut = 0;

    /* -------- 1. Giriş Dosyası Doğrulama -------- */
    for (int i = 0; i < dosya_sayisi; i++) {
        struct stat durum;

        /* Dosya erişilebilir mi? */
        if (stat(dosyalar[i], &durum) < 0) {
            fprintf(stderr,
                    "%s giriş dosyasının formatı uyumsuzdur!\n", dosyalar[i]);
            return -1;  /* Uyumsuz dosya: hata mesajı ver ve hemen çık */
        }

        /* Düzenli (regular) dosya mı? (dizin, sembolik bağ vs. reddedilir) */
        if (!S_ISREG(durum.st_mode)) {
            fprintf(stderr,
                    "%s giriş dosyasının formatı uyumsuzdur!\n", dosyalar[i]);
            return -1;  /* Uyumsuz dosya: hata mesajı ver ve hemen çık */
        }

        /* ASCII metin dosyası mı? */
        if (!metin_dosyasi_mi(dosyalar[i])) {
            fprintf(stderr,
                    "%s giriş dosyasının formatı uyumsuzdur!\n", dosyalar[i]);
            return -1;  /* Uyumsuz dosya: hata mesajı ver ve hemen çık */
        }

        /* Dosya adı uzunluk sınırı */
        if (strlen(dosyalar[i]) >= MAX_DOSYA_ADI) {
            fprintf(stderr,
                    "%s giriş dosyasının formatı uyumsuzdur!\n", dosyalar[i]);
            return -1;  /* Uyumsuz dosya: hata mesajı ver ve hemen çık */
        }

        /* Toplam boyut 200 MB sınırını aşıyor mu? */
        toplam_boyut += (long long)durum.st_size;
        if (toplam_boyut > MAX_TOPLAM_BOYUT) {
            fprintf(stderr,
                    "Hata: Toplam giriş boyutu 200 MB sınırını aşıyor!\n");
            return -1;
        }

        /* Kaydı diziye ekle */
        strncpy(kayitlar[gecerli_sayi].ad,
                dosyalar[i], MAX_DOSYA_ADI - 1);
        kayitlar[gecerli_sayi].ad[MAX_DOSYA_ADI - 1] = '\0';
        kayitlar[gecerli_sayi].izinler = durum.st_mode & (mode_t)0777;
        kayitlar[gecerli_sayi].boyut   = durum.st_size;
        gecerli_sayi++;
    }

    /* Geçerli dosya kalmadıysa çık */
    if (gecerli_sayi == 0) {
        fprintf(stderr,
                "Hata: Arşivlenecek geçerli ASCII metin dosyası bulunamadı!\n");
        return -1;
    }

    /* -------- 2. Organizasyon Bölümünü Oluştur -------- */
    int   org_uzunluk = 0;
    char *org         = organizasyon_bolumu_olustur(kayitlar,
                                                     gecerli_sayi,
                                                     &org_uzunluk);
    if (!org) return -1;

    /*
     * Bölüm-1 toplam boyutu = 10 (başlık alanı) + org_uzunluk
     * Bu değer 10 karakterlik ASCII sayı olarak dosyanın başına yazılır.
     */
    int  bolum1_boyu = BASLIK_ALANI_BOYU + org_uzunluk;
    char boyut_str[BASLIK_ALANI_BOYU + 2];
    snprintf(boyut_str, sizeof(boyut_str), "%010d", bolum1_boyu);

    /* -------- 3. Çıktı Dosyasını Yaz -------- */
    int cikti_fd = open(cikti_dosyasi,
                        O_WRONLY | O_CREAT | O_TRUNC, (mode_t)0644);
    if (cikti_fd < 0) {
        fprintf(stderr, "Hata: '%s' oluşturulamadı: %s\n",
                cikti_dosyasi, strerror(errno));
        free(org);
        return -1;
    }

    /* 3a. Bölüm 1: 10 baytlık boyut alanı */
    if (guvensiz_yaz(cikti_fd, boyut_str, BASLIK_ALANI_BOYU) < 0) {
        fprintf(stderr, "Hata: Başlık alanı yazılamadı!\n");
        free(org);
        close(cikti_fd);
        return -1;
    }

    /* 3b. Bölüm 1: Organizasyon kayıtları */
    if (guvensiz_yaz(cikti_fd, org, (size_t)org_uzunluk) < 0) {
        fprintf(stderr, "Hata: Organizasyon bölümü yazılamadı!\n");
        free(org);
        close(cikti_fd);
        return -1;
    }
    free(org);

    /* 3c. Bölüm 2: Dosya içeriklerini art arda yaz */
    for (int i = 0; i < gecerli_sayi; i++) {
        int kaynak_fd = open(kayitlar[i].ad, O_RDONLY);
        if (kaynak_fd < 0) {
            fprintf(stderr, "Hata: '%s' açılamadı: %s\n",
                    kayitlar[i].ad, strerror(errno));
            close(cikti_fd);
            return -1;
        }

        char    tampon[TAMPON_BOYU];
        ssize_t okunan;
        while ((okunan = read(kaynak_fd, tampon, sizeof(tampon))) > 0) {
            if (guvensiz_yaz(cikti_fd, tampon, (size_t)okunan) < 0) {
                fprintf(stderr, "Hata: Dosya içeriği yazılamadı!\n");
                close(kaynak_fd);
                close(cikti_fd);
                return -1;
            }
        }
        close(kaynak_fd);
    }

    close(cikti_fd);

    printf("Arşiv başarıyla oluşturuldu: %s  (%d dosya, %.2f KB)\n",
           cikti_dosyasi,
           gecerli_sayi,
           (double)toplam_boyut / 1024.0);
    return 0;
}

/* =================== ÇIKARMA FONKSİYONLARI ================ */

/*
 * organizasyon_bolumunu_ayristir()
 * ---------------------------------
 * Ham organizasyon metnini DosyaKaydi dizisine dönüştürür.
 * Başarıda ayrıştırılan kayıt sayısını, hata durumunda -1 döner.
 */
static int organizasyon_bolumunu_ayristir(char *org,
                                          int org_uzunluk,
                                          DosyaKaydi *kayitlar,
                                          int maks_kayit)
{
    int   kayit_sayisi = 0;
    char *p            = org;
    char *son          = org + org_uzunluk;

    while (p < son && kayit_sayisi < maks_kayit) {

        /* '|' açılış karakterini ara */
        if (*p != '|') {
            p++;
            continue;
        }
        p++; /* '|' karakterini geç */

        /* Kapanış '|' karakterini bul */
        char *kapama = memchr(p, '|', (size_t)(son - p));
        if (!kapama) break; /* Bozuk format */

        /* Kaydı geçici tampona aktar */
        int   uzunluk = (int)(kapama - p);
        char  gecici[MAX_DOSYA_ADI + 48];
        if (uzunluk <= 0 || uzunluk >= (int)sizeof(gecici)) {
            return -1; /* Geçersiz kayıt uzunluğu */
        }
        memcpy(gecici, p, (size_t)uzunluk);
        gecici[uzunluk] = '\0';

        /*
         * Kaydı virgülle ayır: dosyaadi,izinler,boyut
         * strtok() gecici[] üzerinde çalışır, orijinale dokunmaz.
         */
        char *dosya_adi  = strtok(gecici, ",");
        char *izin_str   = strtok(NULL,   ",");
        char *boyut_str  = strtok(NULL,   ",");

        if (!dosya_adi || !izin_str || !boyut_str) {
            return -1; /* Eksik alan */
        }

        /* Yapıya yükle */
        strncpy(kayitlar[kayit_sayisi].ad,
                dosya_adi, MAX_DOSYA_ADI - 1);
        kayitlar[kayit_sayisi].ad[MAX_DOSYA_ADI - 1] = '\0';

        /* İzinler: octal string → mode_t */
        kayitlar[kayit_sayisi].izinler =
            (mode_t)strtol(izin_str, NULL, 8);

        /* Boyut: decimal string → off_t */
        kayitlar[kayit_sayisi].boyut =
            (off_t)atoll(boyut_str);

        /* Negatif boyut geçersiz */
        if (kayitlar[kayit_sayisi].boyut < 0) return -1;

        kayit_sayisi++;
        p = kapama + 1; /* Sonraki kayda geç */
    }

    return kayit_sayisi;
}

/*
 * arsiv_ac()
 * ----------
 * -a modunun ana fonksiyonu.
 * Verilen .sau arşivini okur, organizasyon bölümünü ayrıştırır
 * ve dosyaları hedef dizine orijinal izinleriyle çıkarır.
 *
 * arsiv_dosyasi: .sau uzantılı arşiv dosyası yolu
 * hedef_dizin  : çıkarma hedefi (NULL ise geçerli dizin)
 *
 * Başarıda 0, hata durumunda -1 döner.
 */
static int arsiv_ac(const char *arsiv_dosyasi, const char *hedef_dizin)
{
    /* -------- 1. Arşiv Dosyasını Aç -------- */
    int arsiv_fd = open(arsiv_dosyasi, O_RDONLY);
    if (arsiv_fd < 0) {
        fprintf(stderr, "Arşiv dosyası uygunsuz veya bozuk!\n");
        return -1;
    }

    /* -------- 2. İlk 10 Baytı Oku: Bölüm-1 Boyutu -------- */
    char boyut_str[BASLIK_ALANI_BOYU + 2];
    memset(boyut_str, 0, sizeof(boyut_str));

    if (read(arsiv_fd, boyut_str, BASLIK_ALANI_BOYU) != BASLIK_ALANI_BOYU) {
        fprintf(stderr, "Arşiv dosyası uygunsuz veya bozuk!\n");
        close(arsiv_fd);
        return -1;
    }
    boyut_str[BASLIK_ALANI_BOYU] = '\0';

    /* Tüm karakterlerin rakam olduğunu doğrula */
    for (int i = 0; i < BASLIK_ALANI_BOYU; i++) {
        if (!isdigit((unsigned char)boyut_str[i])) {
            fprintf(stderr, "Arşiv dosyası uygunsuz veya bozuk!\n");
            close(arsiv_fd);
            return -1;
        }
    }

    int bolum1_boyu = atoi(boyut_str);
    int org_uzunluk = bolum1_boyu - BASLIK_ALANI_BOYU;

    /* Mantıksal geçerlilik kontrolü */
    if (bolum1_boyu < BASLIK_ALANI_BOYU || org_uzunluk <= 0) {
        fprintf(stderr, "Arşiv dosyası uygunsuz veya bozuk!\n");
        close(arsiv_fd);
        return -1;
    }

    /* -------- 3. Organizasyon Bölümünü Oku -------- */
    char *org = malloc((size_t)(org_uzunluk + 1));
    if (!org) {
        fprintf(stderr, "Hata: Bellek ayırma başarısız!\n");
        close(arsiv_fd);
        return -1;
    }

    ssize_t okunan_org = read(arsiv_fd, org, (size_t)org_uzunluk);
    if (okunan_org != (ssize_t)org_uzunluk) {
        fprintf(stderr, "Arşiv dosyası uygunsuz veya bozuk!\n");
        free(org);
        close(arsiv_fd);
        return -1;
    }
    org[org_uzunluk] = '\0';

    /* -------- 4. Organizasyon Bölümünü Ayrıştır -------- */
    DosyaKaydi kayitlar[MAX_DOSYA_SAYISI];
    int kayit_sayisi = organizasyon_bolumunu_ayristir(org,
                                                       org_uzunluk,
                                                       kayitlar,
                                                       MAX_DOSYA_SAYISI);
    free(org);

    if (kayit_sayisi <= 0) {
        fprintf(stderr, "Arşiv dosyası uygunsuz veya bozuk!\n");
        close(arsiv_fd);
        return -1;
    }

    /* -------- 5. Hedef Dizini Hazırla -------- */
    if (hedef_dizin != NULL && strlen(hedef_dizin) > 0) {
        if (dizin_olustur_veya_dogrula(hedef_dizin) < 0) {
            close(arsiv_fd);
            return -1;
        }
    }

    /* -------- 6. Dosyaları Çıkar -------- */
    for (int i = 0; i < kayit_sayisi; i++) {

        /*
         * Güvenlik: Arşivde saklanan tam yol yerine sadece
         * taban adı (basename) kullanılır; böylece dizin geçişi
         * (path traversal) saldırısı engellenir.
         */
        const char *taban = taban_ad(kayitlar[i].ad);

        /* Çıktı yolunu oluştur */
        char cikti_yolu[MAX_DOSYA_ADI + MAX_DOSYA_ADI + 2];
        if (hedef_dizin != NULL && strlen(hedef_dizin) > 0) {
            snprintf(cikti_yolu, sizeof(cikti_yolu),
                     "%s/%s", hedef_dizin, taban);
        } else {
            strncpy(cikti_yolu, taban, sizeof(cikti_yolu) - 1);
            cikti_yolu[sizeof(cikti_yolu) - 1] = '\0';
        }

        /* Dosyayı orijinal izinlerle oluştur */
        int cikti_fd = open(cikti_yolu,
                            O_WRONLY | O_CREAT | O_TRUNC,
                            kayitlar[i].izinler);
        if (cikti_fd < 0) {
            fprintf(stderr,
                    "Hata: '%s' oluşturulamadı: %s\n",
                    cikti_yolu, strerror(errno));
            close(arsiv_fd);
            return -1;
        }

        /* Dosya içeriğini arşivden oku ve yaz */
        off_t kalan = kayitlar[i].boyut;
        char  tampon[TAMPON_BOYU];

        while (kalan > 0) {
            ssize_t okuyacak = (kalan < (off_t)sizeof(tampon))
                                ? (ssize_t)kalan
                                : (ssize_t)sizeof(tampon);

            ssize_t okunan_veri = read(arsiv_fd, tampon, (size_t)okuyacak);
            if (okunan_veri <= 0) {
                fprintf(stderr, "Arşiv dosyası uygunsuz veya bozuk!\n");
                close(cikti_fd);
                close(arsiv_fd);
                return -1;
            }

            if (guvensiz_yaz(cikti_fd, tampon, (size_t)okunan_veri) < 0) {
                fprintf(stderr,
                        "Hata: '%s' yazılamadı: %s\n",
                        cikti_yolu, strerror(errno));
                close(cikti_fd);
                close(arsiv_fd);
                return -1;
            }
            kalan -= okunan_veri;
        }
        close(cikti_fd);

        /*
         * chmod() ile izinleri kesin olarak ayarla.
         * open() çağrısında umask etkisi izinleri kırpabilir;
         * chmod() bu etkiyi ortadan kaldırır.
         */
        if (chmod(cikti_yolu, kayitlar[i].izinler) < 0) {
            fprintf(stderr,
                    "Uyarı: '%s' için izinler ayarlanamadı: %s\n",
                    cikti_yolu, strerror(errno));
            /* İzin hatası kritik değil, devam et */
        }

        printf("Çıkarıldı: %-40s  izin: %04o  boyut: %lld bayt\n",
               cikti_yolu,
               (unsigned int)kayitlar[i].izinler,
               (long long)kayitlar[i].boyut);
    }

    close(arsiv_fd);
    printf("\nArşiv başarıyla açıldı: %d dosya çıkarıldı.\n", kayit_sayisi);
    return 0;
}

/* ========================= KULLANIM ======================== */

static void kullanim_goster(const char *program_adi)
{
    fprintf(stderr,
        "\nKullanım:\n"
        "  Arşivleme : %s -b <d1> [d2 ... d32] [-o <cikti.sau>]\n"
        "  Çıkarma   : %s -a <arsiv.sau> [hedef_dizin]\n\n"
        "Kısıtlamalar:\n"
        "  * En fazla %d giriş dosyası arşivlenebilir\n"
        "  * Toplam giriş boyutu en fazla 200 MB olabilir\n"
        "  * Yalnızca ASCII (7-bit) metin dosyaları kabul edilir\n"
        "  * -o belirtilmezse çıktı adı varsayılan olarak 'a.sau' olur\n\n",
        program_adi, program_adi, MAX_DOSYA_SAYISI);
}

/* ========================= MAIN ============================ */

int main(int argc, char *argv[])
{
    if (argc < 2) {
        kullanim_goster(argv[0]);
        return EXIT_FAILURE;
    }

    /* ===================================================
     * MOD: Arşivleme  →  tarsau -b dosya1 ... [-o cikti.sau]
     * =================================================== */
    if (strcmp(argv[1], "-b") == 0) {

        if (argc < 3) {
            fprintf(stderr,
                    "Hata: -b parametresi ile en az bir dosya belirtilmeli!\n");
            kullanim_goster(argv[0]);
            return EXIT_FAILURE;
        }

        char       *dosyalar[MAX_DOSYA_SAYISI];
        int         dosya_sayisi  = 0;
        const char *cikti_dosyasi = "a.sau"; /* Varsayılan arşiv adı */

        /* Komut satırı argümanlarını tara */
        for (int i = 2; i < argc; i++) {
            if (strcmp(argv[i], "-o") == 0) {
                /* Çıktı dosyası adı: -o'dan sonraki argüman */
                if (i + 1 >= argc) {
                    fprintf(stderr,
                            "Hata: -o seçeneğinden sonra dosya adı gerekli!\n");
                    return EXIT_FAILURE;
                }
                cikti_dosyasi = argv[++i];

            } else {
                /* Giriş dosyası */
                if (dosya_sayisi >= MAX_DOSYA_SAYISI) {
                    fprintf(stderr,
                            "Hata: En fazla %d dosya arşivlenebilir!\n",
                            MAX_DOSYA_SAYISI);
                    return EXIT_FAILURE;
                }
                dosyalar[dosya_sayisi++] = argv[i];
            }
        }

        if (dosya_sayisi == 0) {
            fprintf(stderr,
                    "Hata: Arşivlenecek en az bir dosya belirtilmeli!\n");
            kullanim_goster(argv[0]);
            return EXIT_FAILURE;
        }

        return (arsivle(dosyalar, dosya_sayisi, cikti_dosyasi) == 0)
               ? EXIT_SUCCESS : EXIT_FAILURE;

    /* ===================================================
     * MOD: Çıkarma  →  tarsau -a arsiv.sau [hedef_dizin]
     * =================================================== */
    } else if (strcmp(argv[1], "-a") == 0) {

        if (argc < 3) {
            fprintf(stderr,
                    "Hata: -a parametresi ile arşiv dosyası belirtilmeli!\n");
            kullanim_goster(argv[0]);
            return EXIT_FAILURE;
        }

        /* -a'dan sonra en fazla 2 ek parametre alınabilir */
        if (argc > 4) {
            fprintf(stderr,
                    "Hata: -a seçeneği en fazla 2 ek parametre alabilir!\n");
            kullanim_goster(argv[0]);
            return EXIT_FAILURE;
        }

        const char *arsiv_dosyasi = argv[2];
        const char *hedef_dizin   = (argc >= 4) ? argv[3] : NULL;

        return (arsiv_ac(arsiv_dosyasi, hedef_dizin) == 0)
               ? EXIT_SUCCESS : EXIT_FAILURE;

    /* ===================================================
     * Geçersiz Parametre
     * =================================================== */
    } else {
        fprintf(stderr, "Hata: Geçersiz parametre: '%s'\n", argv[1]);
        kullanim_goster(argv[0]);
        return EXIT_FAILURE;
    }
}
