# XIAO ESP32-S3 — ICVSP BLE firmware starter

## ده بيعمل إيه؟
مشروع ESP-IDF حقيقي المصدر، مخصص لـESP32-S3، بيجهّز خدمة BLE لإرسال
بايتات التقرير من البورد للموبايل، وقناة ACK لاستقبال تأكيد الحفظ.
ده كود بداية للدمج والاختبار، وليس Firmware نهائيًا جاهزًا للنشر.

البورد غير متاحة في الجلسة وESP-IDF غير مثبت في بيئة إعداد الملفات، لذلك
لم يتم بناء نسخة ESP32 أو رفعها أو اختبار الراديو. اختبارات منطق النقل على
الكمبيوتر نجحت. التوافق مع نسخة ESP-IDF 5.5.5 عندك يحتاج تأكيد بأول build.
لا يوجد ملف bin مُختبر ضمن التسليم.

## الموجود
- NimBLE peripheral يعلن عن ICVSP-001 وخدمة المشروع.
- Report Data للإشعارات وReport ACK للكتابة.
- تقسيم حسب MTU، مع حفظ البايتات كما هي.
- طابور RAM، مهلة ACK، إعادة إرسال، وتعامل مع إعادة الاتصال.
- نقطة دمج مع كود التوقيع: icvsp_ble_enqueue().
- برنامج استقبال Python اختياري لاختبار البورد لاحقًا؛ ليس تطبيق Flutter.
- اختبارات C لمنطق النقل واختبارات Python للتجميع والحفظ.

## أول خطوة عندك الآن: البناء بدون بورد
فكي الضغط في مسار بسيط مثل C:\ICVSP_XIAO_BLE.
افتحي ESP-IDF 5.5 Command Prompt أو شغلي export.bat الخاص بتثبيتك.
ثم اكتبي:

```cmd
cd /d C:\ICVSP_XIAO_BLE
idf.py --version
idf.py set-target esp32s3
idf.py build
```

لا تحتاجي البورد لأمر build. ابعتي أول خطأ إن حصل؛ لا تختاري erase-flash
كحل عام، لأن دمج المشروع لاحقًا قد يتضمن مفاتيح وهوية في NVS.
الإعدادات الافتراضية لا تولّد تقارير: تنتظر كود المشروع يستدعي دالة الإدخال.

## تشغيل رسالة اختبار عند توفر البورد
قبل البناء افتحي:

```cmd
idf.py menuconfig
```

من ICVSP BLE فعّلي Queue one UNSIGNED 706-byte transport test at boot.
دي رسالة نقل تشخيصية غير موقّعة، وليست تقرير حفرة ولا صيغة firmware v2.
ثم ابني، وصلي البورد بـUSB data cable. استبدلي COM7 بمنفذها الفعلي:

```cmd
idf.py build
idf.py -p COM7 flash monitor
```

إغلاق monitor: Ctrl+] . اختير USB Serial/JTAG في sdkconfig.defaults؛
راجعي منفذ Windows وطريقة إقلاع البورد إن لم تظهر. لا تمسحي الفلاش تلقائيًا.

## استقبال على اللابتوب للاختبار لاحقًا
من جذر المشروع في CMD عادي:

```cmd
py -m venv .venv
.venv\Scripts\python -m pip install -r tools\requirements.txt
.venv\Scripts\python tools\receive_ble.py
```

البرنامج يبحث عن البورد بالخدمة (لا يبحث عن Galaxy A13)، يشترك في Notify،
يجمع أجزاء التقرير، يكتب ملف received/<sha256>.bin ويعمل fsync ثم يرسل ACK.
لا يتصل بـbackend ولا يدّعي أن التوقيع صحيح. يفصل Ctrl+C؛ إعادة تشغيله تعيد
البحث بعد فصل الاتصال. حفظ الملفات يستمر عبر إعادة تشغيل البرنامج.
حتى اختبار Flutter لا يزال مطلوبًا بعد توفر التطبيق والبورد.

## دمج كود التوقيع الموجود
بعد نهاية signing/serialization في مهمة الجهاز:

```c
#include "icvsp_ble.h"

// signed_wire_bytes: actual existing serialized signed envelope
esp_err_t rc = icvsp_ble_enqueue(signed_wire_bytes, signed_wire_length);
// ESP_OK: copied into RAM ingress, NOT delivered/ACKed yet.
// ESP_ERR_TIMEOUT: ingress full; producer must keep a copy and retry later.
// ESP_ERR_INVALID_ARG: NULL/empty/report exceeds 2048 bytes.
```

app_main الحالي يبدأ NVS والـBLE مرة واحدة. عند الدمج مع مشروع موجود، لا
تكرري تهيئة NimBLE/NVS. الدالة من task فقط، وليست ISR. ينصح بمساحة stack
مناسبة (4 KB أو أكثر للمهمة المستدعية) لأن نسخة الإدخال المؤقتة نحو 2 KB.
التوقيع الأصلي ومفاتيح الجهاز غير مضمنة؛ لا تولّدي هوية بديلة عند كل تشغيل.

## اختبارات الكمبيوتر
Python (requires cryptography):

```cmd
.venv\Scripts\python -m unittest discover -s tests -p "test_*.py" -v
```

C على جهاز فيه GCC:

```sh
gcc -std=c11 -Wall -Wextra -Werror -I main main/transport.c tests/test_transport.c -o transport_test
./transport_test
```

راجع TEST_RESULTS.md لما تم اختباره، وPROTOCOL.md للعقد الكامل وحدود الطوابير.
إعادة تشغيل البورد تمسح طوابير RAM؛ التخزين الدائم والربط بمستخدم آمن خطوات
لاحقة. ACK غير محمي بهوية مالك في هذه النسخة؛ ليست جاهزة للاستخدام الميداني.

## Sources used during implementation
- https://github.com/espressif/esp-idf/tree/master/examples/bluetooth/nimble/bleprph
- https://docs.espressif.com/projects/esp-idf/en/release-v5.5/esp32s3/
- https://bleak.readthedocs.io/en/latest/api/client.html

Reference examples retrieved were current upstream examples, not a successful
build of this project against ESP-IDF 5.5.5. Confirm APIs/config with local SDK.
