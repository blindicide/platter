<?xml version="1.0" encoding="utf-8"?>
<!DOCTYPE TS>
<TS version="2.1" language="ru_RU">
<context>
    <name>ConfirmDialog</name>
    <message>
        <source>Target:</source>
        <translation>Цель:</translation>
    </message>
    <message>
        <source>Device:</source>
        <translation>Устройство:</translation>
    </message>
    <message>
        <source>Serial:</source>
        <translation>Серийный №:</translation>
    </message>
    <message>
        <source>Sectors:</source>
        <translation>Секторы:</translation>
    </message>
    <message>
        <source>Operation:</source>
        <translation>Операция:</translation>
    </message>
    <message>
        <source>Range:</source>
        <translation>Диапазон:</translation>
    </message>
    <message>
        <source>Affected:</source>
        <translation>Затронуто:</translation>
    </message>
    <message>
        <source>Warning:</source>
        <translation>Предупреждение:</translation>
    </message>
    <message>
        <source>REFUSED:</source>
        <translation>ОТКАЗ:</translation>
    </message>
    <message>
        <source>%1, %2, %3</source>
        <translation>%1, %2, %3</translation>
    </message>
    <message>
        <source>(unknown model)</source>
        <translation>(модель неизвестна)</translation>
    </message>
    <message>
        <source>%1 B logical / %2 B physical</source>
        <translation>%1 Б логич. / %2 Б физич.</translation>
    </message>
    <message>
        <source>WHOLE DEVICE (%1 sectors, %2)</source>
        <translation>ВСЁ УСТРОЙСТВО (секторов: %1, %2)</translation>
    </message>
    <message>
        <source>LBA %1 - %2 (%3 sectors)</source>
        <translation>LBA %1 – %2 (секторов: %3)</translation>
    </message>
    <message>
        <source>&lt;b&gt;This operation will DESTROY data and cannot be undone.&lt;/b&gt;</source>
        <translation>&lt;b&gt;Эта операция УНИЧТОЖИТ данные, и отменить её будет нельзя.&lt;/b&gt;</translation>
    </message>
    <message>
        <source>&lt;b&gt;This operation is REFUSED.&lt;/b&gt;</source>
        <translation>&lt;b&gt;Эта операция ОТКЛОНЕНА.&lt;/b&gt;</translation>
    </message>
    <message>
        <source>type the confirmation here</source>
        <translation>введите подтверждение здесь</translation>
    </message>
    <message>
        <source>&amp;Destroy Data and Start</source>
        <translation>&amp;Уничтожить данные и начать</translation>
    </message>
    <message>
        <source>DISMOUNT:</source>
        <translation>ОТМОНТИРОВАНИЕ:</translation>
    </message>
    <message>
        <source>the volume mounted at %1 will be locked and dismounted</source>
        <translation>том, смонтированный в %1, будет заблокирован и отмонтирован</translation>
    </message>
    <message>
        <source>I understand that the mounted volumes (%1) will be locked and dismounted, and programs using them will lose unsaved data.</source>
        <translation>Я понимаю, что смонтированные тома (%1) будут заблокированы и отмонтированы, а программы, которые их используют, потеряют несохранённые данные.</translation>
    </message>
</context>
<context>
    <name>HelpDialog</name>
    <message>
        <source>Getting started</source>
        <translation>Начало работы</translation>
    </message>
    <message>
        <source>&lt;h2&gt;Getting started&lt;/h2&gt;&lt;p&gt;platter lists the storage devices it can see on the left of the INFO tab. Pick one and use the tabs to look at it. Everything on the INFO, SMART, SURFACE (read scan), BENCHMARK (read), PARTITIONS (view) and HEX tabs only &lt;b&gt;reads&lt;/b&gt;.&lt;/p&gt;&lt;p&gt;You can also open a raw disk image (&lt;i&gt;File &amp;gt; Open Disk Image&lt;/i&gt;) and use almost every feature on it with no risk to real hardware. That is the best way to try things out.&lt;/p&gt;&lt;p&gt;Reading raw sectors of a physical disk needs administrator rights. On Linux you can either start platter as root or use &lt;i&gt;File &amp;gt; Use Privileged Helper&lt;/i&gt;, which asks for your password and opens the disks through a small helper process while the rest of the program stays unprivileged. On Windows start platter as Administrator.&lt;/p&gt;</source>
        <translation>&lt;h2&gt;Начало работы&lt;/h2&gt;&lt;p&gt;platter показывает доступные ему устройства хранения в левой части вкладки СВЕДЕНИЯ. Выберите устройство и изучайте его на вкладках. Всё на вкладках СВЕДЕНИЯ, SMART, ПОВЕРХНОСТЬ (сканирование чтением), ТЕСТ СКОРОСТИ (чтение), РАЗДЕЛЫ (просмотр) и HEX только &lt;b&gt;читает&lt;/b&gt;.&lt;/p&gt;&lt;p&gt;Можно также открыть «сырой» образ диска (&lt;i&gt;Файл &amp;gt; Открыть образ диска&lt;/i&gt;) и пользоваться почти всеми возможностями на нём без всякого риска для настоящего оборудования. Это лучший способ всё попробовать.&lt;/p&gt;&lt;p&gt;Чтение «сырых» секторов физического диска требует прав администратора. В Linux можно запустить platter от root или воспользоваться пунктом &lt;i&gt;Файл &amp;gt; Использовать привилегированный помощник&lt;/i&gt;: он запросит пароль и откроет диски через небольшой вспомогательный процесс, а остальная программа останется без привилегий. В Windows запускайте platter от имени администратора.&lt;/p&gt;</translation>
    </message>
    <message>
        <source>Safety model</source>
        <translation>Модель безопасности</translation>
    </message>
    <message>
        <source>&lt;h2&gt;Safety model&lt;/h2&gt;&lt;p&gt;Anything that &lt;b&gt;writes&lt;/b&gt; to a device (erase, write scans, write benchmarks, qualification, partition edits, formatting, restoring an image, cloning) follows the same steps:&lt;/p&gt;&lt;ol&gt;&lt;li&gt;A &lt;b&gt;preview&lt;/b&gt; shows exactly which device and which sector range will be changed, and lists every reason it would be refused (mounted, system disk, held by LVM/RAID, read-only...).&lt;/li&gt;&lt;li&gt;You &lt;b&gt;type a confirmation&lt;/b&gt; text. It is not a button you can click through by reflex.&lt;/li&gt;&lt;li&gt;platter looks at the device again, from scratch. If anything differs from the preview (a different disk took the same name, a partition was mounted in the meantime) it stops.&lt;/li&gt;&lt;li&gt;Writes can only land inside the range you confirmed.&lt;/li&gt;&lt;/ol&gt;&lt;p&gt;The system disk and anything mounted are blocked. A disk that is &lt;i&gt;unplugged&lt;/i&gt; is the only completely safe disk: if you have a drive you care about, disconnect it before running destructive operations on another one.&lt;/p&gt;</source>
        <translation>&lt;h2&gt;Модель безопасности&lt;/h2&gt;&lt;p&gt;Всё, что &lt;b&gt;записывает&lt;/b&gt; на устройство (стирание, сканирование с записью, тесты записи, квалификация, правка разделов, форматирование, восстановление образа, клонирование), проходит одни и те же шаги:&lt;/p&gt;&lt;ol&gt;&lt;li&gt;&lt;b&gt;Предпросмотр&lt;/b&gt; показывает, какое именно устройство и какой диапазон секторов будут изменены, и перечисляет все причины, по которым операцию отклонят (смонтировано, системный диск, занято LVM/RAID, только чтение...).&lt;/li&gt;&lt;li&gt;Вы &lt;b&gt;вводите текст подтверждения&lt;/b&gt;. Это не кнопка, которую можно нажать по привычке.&lt;/li&gt;&lt;li&gt;platter заново, с нуля, опрашивает устройство. Если что-то отличается от предпросмотра (другой диск получил то же имя, за это время смонтирован раздел), операция останавливается.&lt;/li&gt;&lt;li&gt;Запись может попасть только в подтверждённый вами диапазон.&lt;/li&gt;&lt;/ol&gt;&lt;p&gt;Системный диск и всё смонтированное заблокированы. Единственный полностью безопасный диск — &lt;i&gt;отключённый&lt;/i&gt;: если у вас есть дорогой вам накопитель, отсоедините его, прежде чем выполнять деструктивные операции с другим.&lt;/p&gt;</translation>
    </message>
    <message>
        <source>Surface scan</source>
        <translation>Сканирование поверхности</translation>
    </message>
    <message>
        <source>&lt;h2&gt;Surface scan&lt;/h2&gt;&lt;p&gt;Reads the disk block by block and colours each block by how long it took. Slow or unreadable blocks are early signs of a failing disk. Read scans are safe. Write scan modes destroy the data in the chosen range and ask for confirmation.&lt;/p&gt;&lt;p&gt;Block size, order (forward, reverse, random, sampled), retries and the latency thresholds are on the tab. Scans can be paused, resumed and saved as JSON, CSV or text. A scan of a disk through the OS cache measures the cache, not the disk: enable direct I/O to bypass it.&lt;/p&gt;</source>
        <translation>&lt;h2&gt;Сканирование поверхности&lt;/h2&gt;&lt;p&gt;Читает диск блок за блоком и раскрашивает каждый блок по времени, которое он занял. Медленные или нечитаемые блоки — ранние признаки умирающего диска. Сканирование чтением безопасно. Режимы сканирования с записью уничтожают данные в выбранном диапазоне и запрашивают подтверждение.&lt;/p&gt;&lt;p&gt;Размер блока, порядок (вперёд, назад, случайный, выборочный), повторы и пороги задержки настраиваются на вкладке. Сканирование можно приостановить, продолжить и сохранить в JSON, CSV или текст. Сканирование диска через кэш ОС измеряет кэш, а не диск: включите прямой ввод-вывод, чтобы обойти его.&lt;/p&gt;</translation>
    </message>
    <message>
        <source>Benchmark</source>
        <translation>Тест производительности</translation>
    </message>
    <message>
        <source>&lt;h2&gt;Benchmark&lt;/h2&gt;&lt;p&gt;Sequential, random, access-time, full-disk speed curve, block-size and queue-depth tests. Results include latency percentiles and explicit warnings when a number may come from a cache or a virtual disk instead of the device.&lt;/p&gt;&lt;p&gt;Write benchmarks need an explicit region and the usual typed confirmation. Queue-depth tests are not meaningful through the privileged helper.&lt;/p&gt;</source>
        <translation>&lt;h2&gt;Тест производительности&lt;/h2&gt;&lt;p&gt;Последовательные и случайные тесты, время доступа, кривая скорости по всему диску, перебор размеров блока и глубины очереди. Результаты включают перцентили задержек и явные предупреждения, когда цифра может происходить из кэша или виртуального диска, а не от самого устройства.&lt;/p&gt;&lt;p&gt;Тесты записи требуют явно заданной области и обычного подтверждения вводом текста. Тесты глубины очереди через привилегированный помощник не имеют смысла.&lt;/p&gt;</translation>
    </message>
    <message>
        <source>SMART</source>
        <translation>SMART</translation>
    </message>
    <message>
        <source>&lt;h2&gt;SMART&lt;/h2&gt;&lt;p&gt;Health data comes from &lt;i&gt;smartmontools&lt;/i&gt; (&lt;code&gt;smartctl&lt;/code&gt; must be installed). platter shows the raw attributes and a deliberately conservative assessment: GOOD, WARNING, CRITICAL or UNKNOWN, with every observation that led to it. A passing SMART status does not guarantee a healthy disk.&lt;/p&gt;</source>
        <translation>&lt;h2&gt;SMART&lt;/h2&gt;&lt;p&gt;Данные о состоянии берутся из &lt;i&gt;smartmontools&lt;/i&gt; (должен быть установлен &lt;code&gt;smartctl&lt;/code&gt;). platter показывает сырые атрибуты и намеренно осторожную оценку: ХОРОШО, ПРЕДУПРЕЖДЕНИЕ, КРИТИЧНО или НЕИЗВЕСТНО — со всеми наблюдениями, которые к ней привели. Успешный статус SMART не гарантирует здоровье диска.&lt;/p&gt;</translation>
    </message>
    <message>
        <source>Hex viewer</source>
        <translation>Hex-просмотр</translation>
    </message>
    <message>
        <source>&lt;h2&gt;Hex viewer&lt;/h2&gt;&lt;p&gt;Shows sectors read-only, decodes what it recognises there (MBR, GPT, FAT, exFAT, NTFS and ext structures) and searches a range for bytes, ASCII or UTF-16 text. Nothing is read until you press &lt;b&gt;Go&lt;/b&gt;; selecting a device never reads it.&lt;/p&gt;</source>
        <translation>&lt;h2&gt;Hex-просмотр&lt;/h2&gt;&lt;p&gt;Показывает секторы только для чтения, расшифровывает распознанное (структуры MBR, GPT, FAT, exFAT, NTFS и ext) и ищет в диапазоне байты, текст ASCII или UTF-16. Ничего не читается, пока вы не нажмёте &lt;b&gt;Перейти&lt;/b&gt;; выбор устройства сам по себе его никогда не читает.&lt;/p&gt;</translation>
    </message>
    <message>
        <source>Erase</source>
        <translation>Стирание</translation>
    </message>
    <message>
        <source>&lt;h2&gt;Erase&lt;/h2&gt;&lt;p&gt;Overwrites a whole device or a range with zeros, ones, random data, multiple passes, DoD-style patterns or a custom pattern, then reads it back to verify. Reports say honestly what was and was not verified.&lt;/p&gt;&lt;p&gt;&lt;b&gt;Limits:&lt;/b&gt; overwriting cannot reach sectors the drive has remapped, over-provisioned flash, or hidden areas. Hardware secure erase commands (ATA Secure Erase, NVMe format/sanitize) are intentionally not implemented.&lt;/p&gt;</source>
        <translation>&lt;h2&gt;Стирание&lt;/h2&gt;&lt;p&gt;Перезаписывает всё устройство или диапазон нулями, единицами, случайными данными, в несколько проходов, шаблонами в стиле DoD или своим шаблоном, затем читает обратно для проверки. Отчёты честно сообщают, что было проверено, а что нет.&lt;/p&gt;&lt;p&gt;&lt;b&gt;Ограничения:&lt;/b&gt; перезапись не достаёт секторов, которые накопитель переназначил, резервной области флеш-памяти и скрытых областей. Аппаратные команды безопасного стирания (ATA Secure Erase, NVMe format/sanitize) намеренно не реализованы.&lt;/p&gt;</translation>
    </message>
    <message>
        <source>Qualification</source>
        <translation>Квалификация</translation>
    </message>
    <message>
        <source>&lt;h2&gt;Drive qualification&lt;/h2&gt;&lt;p&gt;A burn-in test for a drive you are about to trust: several full write passes with read-back, a final read pass, random reads, and a SMART before/after comparison, ending in PASS / PASS WITH WARNINGS / FAIL. It destroys all data on the drive. The thresholds are engineering judgement, not a standard: read the listed observations, not just the verdict.&lt;/p&gt;</source>
        <translation>&lt;h2&gt;Квалификация накопителя&lt;/h2&gt;&lt;p&gt;Прогон под нагрузкой для накопителя, которому вы собираетесь доверять: несколько полных проходов записи с контрольным чтением, итоговый проход чтения, случайные чтения и сравнение SMART до и после; заканчивается заключением ПРОЙДЕНО / ПРОЙДЕНО С ПРЕДУПРЕЖДЕНИЯМИ / НЕ ПРОЙДЕНО. Все данные на накопителе уничтожаются. Пороги — инженерная оценка, а не стандарт: читайте перечисленные наблюдения, а не только заключение.&lt;/p&gt;</translation>
    </message>
    <message>
        <source>Partitions and formatting</source>
        <translation>Разделы и форматирование</translation>
    </message>
    <message>
        <source>&lt;h2&gt;Partitions and formatting&lt;/h2&gt;&lt;p&gt;View GPT/MBR tables (with CRC and overlap checks) on the PARTITIONS tab. &lt;i&gt;Device &amp;gt; Edit Partitions&lt;/i&gt; stages changes in memory and writes them only after the typed confirmation. Quick formatting to FAT32 and exFAT is built in; NTFS and ext4 are not.&lt;/p&gt;</source>
        <translation>&lt;h2&gt;Разделы и форматирование&lt;/h2&gt;&lt;p&gt;Таблицы GPT/MBR (с проверкой CRC и пересечений) можно смотреть на вкладке РАЗДЕЛЫ. Пункт &lt;i&gt;Устройство &amp;gt; Изменить разделы&lt;/i&gt; накапливает изменения в памяти и записывает их только после подтверждения вводом текста. Быстрое форматирование в FAT32 и exFAT встроено; NTFS и ext4 — нет.&lt;/p&gt;</translation>
    </message>
    <message>
        <source>Imaging and cloning</source>
        <translation>Образы и клонирование</translation>
    </message>
    <message>
        <source>&lt;h2&gt;Clone / Image&lt;/h2&gt;&lt;p&gt;Create an image of a device (raw or zstd, with a SHA-256 recorded), restore an image to a device, clone one device onto another, or verify an image against its checksum or a device. With &lt;i&gt;recover&lt;/i&gt; enabled, unreadable sectors are zero-filled, counted and listed, and the result is flagged so a damaged copy never looks perfect.&lt;/p&gt;</source>
        <translation>&lt;h2&gt;Клон / образ&lt;/h2&gt;&lt;p&gt;Создание образа устройства («сырого» или zstd, с записью SHA-256), восстановление образа на устройство, клонирование одного устройства на другое, а также проверка образа по контрольной сумме или по устройству. При включённом режиме &lt;i&gt;восстановления&lt;/i&gt; нечитаемые секторы заполняются нулями, подсчитываются и перечисляются, а результат помечается, чтобы повреждённая копия никогда не выглядела безупречной.&lt;/p&gt;</translation>
    </message>
    <message>
        <source>History</source>
        <translation>История</translation>
    </message>
    <message>
        <source>&lt;h2&gt;History&lt;/h2&gt;&lt;p&gt;Finished benchmarks, scans, SMART queries, erases, imaging sessions and qualifications are saved to a local SQLite database (&lt;i&gt;Tools &amp;gt; Operation History&lt;/i&gt;), shared with &lt;code&gt;platter-cli&lt;/code&gt;. Serial numbers are never stored. Turn recording off in Preferences. Deleting a history record never touches a device.&lt;/p&gt;</source>
        <translation>&lt;h2&gt;История&lt;/h2&gt;&lt;p&gt;Завершённые тесты, сканирования, запросы SMART, стирания, сеансы создания образов и квалификации сохраняются в локальной базе SQLite (&lt;i&gt;Сервис &amp;gt; История операций&lt;/i&gt;), общей с &lt;code&gt;platter-cli&lt;/code&gt;. Серийные номера не сохраняются никогда. Запись можно отключить в параметрах. Удаление записи истории никогда не затрагивает устройство.&lt;/p&gt;</translation>
    </message>
</context>
<context>
    <name>QObject</name>
    <message>
        <source>Export to CSV</source>
        <translation>Экспорт в CSV</translation>
    </message>
    <message>
        <source>CSV files (*.csv);;All files (*)</source>
        <translation>Файлы CSV (*.csv);;Все файлы (*)</translation>
    </message>
    <message>
        <source>Cannot write %1:
%2</source>
        <translation>Не удаётся записать %1:
%2</translation>
    </message>
    <message>
        <source>&amp;Copy</source>
        <translation>&amp;Копировать</translation>
    </message>
    <message>
        <source>Select &amp;All</source>
        <translation>Выделить &amp;всё</translation>
    </message>
    <message>
        <source>&amp;Export to CSV...</source>
        <translation>&amp;Экспорт в CSV...</translation>
    </message>
</context>
<context>
    <name>core</name>
    <message>
        <source>range %1+%2 lies outside the device (%3 sectors)</source>
        <translation>диапазон %1+%2 выходит за пределы устройства (секторов: %3)</translation>
    </message>
    <message>
        <source>a volume is mounted at %1</source>
        <translation>том смонтирован в %1</translation>
    </message>
    <message>
        <source>in use by %1 (volume manager / encryption / RAID)</source>
        <translation>используется: %1 (менеджер томов / шифрование / RAID)</translation>
    </message>
    <message>
        <source>Data in the selected range of %1 will be destroyed and cannot be recovered.</source>
        <translation>Данные в выбранном диапазоне устройства %1 будут уничтожены и не подлежат восстановлению.</translation>
    </message>
    <message>
        <source>To confirm, type the image file name: %1</source>
        <translation>Для подтверждения введите имя файла образа: %1</translation>
    </message>
    <message>
        <source>To confirm, type the device name: %1</source>
        <translation>Для подтверждения введите имя устройства: %1</translation>
    </message>
    <message>
        <source>To confirm, type the model name (%1) or the last 4 characters of the serial number (%2)</source>
        <translation>Для подтверждения введите название модели (%1) или последние 4 символа серийного номера (%2)</translation>
    </message>
    <message>
        <source>To confirm, type the model name (%1)</source>
        <translation>Для подтверждения введите название модели (%1)</translation>
    </message>
    <message>
        <source>To confirm, type the model name or the last 4 characters of the serial number (%1)</source>
        <translation>Для подтверждения введите название модели или последние 4 символа серийного номера (%1)</translation>
    </message>
    <message>
        <source>the operation is blocked: %1</source>
        <translation>операция заблокирована: %1</translation>
    </message>
    <message>
        <source>the operation is now blocked: %1</source>
        <translation>операция теперь заблокирована: %1</translation>
    </message>
    <message>
        <source>the device could not be re-examined: %1</source>
        <translation>не удалось повторно опросить устройство: %1</translation>
    </message>
    <message>
        <source>the device is not the one that was previewed (identity changed: now %1)</source>
        <translation>это не то устройство, которое было показано при предпросмотре (идентичность изменилась: теперь %1)</translation>
    </message>
    <message>
        <source>the opened device does not match the previewed identity (%1)</source>
        <translation>открытое устройство не совпадает с показанным при предпросмотре (%1)</translation>
    </message>
    <message>
        <source>write at byte offset %1 (%2 bytes) is outside the confirmed range</source>
        <translation>запись по смещению %1 байт (%2 байт) выходит за подтверждённый диапазон</translation>
    </message>
    <message>
        <source>discard at byte offset %1 is outside the confirmed range</source>
        <translation>discard по смещению %1 байт выходит за подтверждённый диапазон</translation>
    </message>
    <message>
        <source>image file not found: %1</source>
        <translation>файл образа не найден: %1</translation>
    </message>
    <message>
        <source>could not open exclusively for writing: %1</source>
        <translation>не удалось открыть монопольно для записи: %1</translation>
    </message>
    <message>
        <source>Attribute %1 %2 is at or below its threshold now</source>
        <translation>Атрибут %1 %2 сейчас достиг порога или ниже него</translation>
    </message>
    <message>
        <source>Attribute %1 %2 has been below its threshold in the past</source>
        <translation>Атрибут %1 %2 раньше опускался ниже порога</translation>
    </message>
    <message>
        <source>%1 %2 (attribute %3)</source>
        <translation>%2: %1 (атрибут %3)</translation>
    </message>
    <message>
        <source>%1 interface CRC errors (attribute 199): usually cabling or the controller/bridge, not the media</source>
        <translation>Ошибок CRC интерфейса: %1 (атрибут 199); обычно виноваты кабель или контроллер/мост, а не носитель</translation>
    </message>
    <message>
        <source>NVMe critical warning flags set:%1</source>
        <translation>Установлены флаги критического предупреждения NVMe:%1</translation>
    </message>
    <message>
        <source>Available spare %1% is below the threshold of %2%</source>
        <translation>Доступный резерв %1% ниже порога %2%</translation>
    </message>
    <message>
        <source>Rated endurance exceeded (percentage used %1%)</source>
        <translation>Расчётный ресурс исчерпан (использовано %1%)</translation>
    </message>
    <message>
        <source>Approaching rated endurance (percentage used %1%)</source>
        <translation>Расчётный ресурс почти исчерпан (использовано %1%)</translation>
    </message>
    <message>
        <source>%1 media and data integrity errors</source>
        <translation>Ошибок носителя и целостности данных: %1</translation>
    </message>
    <message>
        <source>%1 unsafe shutdowns recorded (informational)</source>
        <translation>Нештатных выключений зафиксировано: %1 (справочно)</translation>
    </message>
    <message>
        <source>Most recent self-test failed: %1</source>
        <translation>Последний самотест завершился сбоем: %1</translation>
    </message>
    <message>
        <source>ATA error log contains %1 entries</source>
        <translation>Журнал ошибок ATA содержит записей: %1</translation>
    </message>
    <message>
        <source>Temperature %1 °C is at or above %2 °C</source>
        <translation>Температура %1 °C — не ниже %2 °C</translation>
    </message>
    <message>
        <source>smartctl could not open or query the device (exit status %1)</source>
        <translation>smartctl не смог открыть устройство или опросить его (код завершения %1)</translation>
    </message>
    <message>
        <source>smartctl: %1</source>
        <translation>smartctl: %1</translation>
    </message>
    <message>
        <source>%1 rose from %2 to %3 %4</source>
        <translation>%1: рост с %2 до %3 %4</translation>
    </message>
    <message>
        <source>NVMe critical warning bits are set after the run (0x%1)</source>
        <translation>После прогона установлены биты критического предупреждения NVMe (0x%1)</translation>
    </message>
    <message>
        <source>The drive reported %1 °C after the run</source>
        <translation>После прогона накопитель сообщил температуру %1 °C</translation>
    </message>
    <message>
        <source>%1: %2 sector(s) could not be written</source>
        <translation>%1: не удалось записать секторов: %2</translation>
    </message>
    <message>
        <source>%1: %2 sector(s) could not be read</source>
        <translation>%1: не удалось прочитать секторов: %2</translation>
    </message>
    <message>
        <source>%1: %2 sector(s) read back different from what was written</source>
        <translation>%1: секторов, прочитанных не так, как записано: %2</translation>
    </message>
    <message>
        <source>%1: %2 of %3 random reads took %4 ms or longer</source>
        <translation>%1: %2 из %3 случайных чтений заняли %4 мс или дольше</translation>
    </message>
    <message>
        <source>%1: %2 of %3 blocks took %4 ms or longer</source>
        <translation>%1: %2 из %3 блоков заняли %4 мс или дольше</translation>
    </message>
    <message>
        <source>%1: %2 random reads took %3 ms or longer (within tolerance)</source>
        <translation>%1: случайных чтений, занявших %3 мс или дольше: %2 (в пределах допуска)</translation>
    </message>
    <message>
        <source>%1: %2 blocks took %3 ms or longer (within tolerance)</source>
        <translation>%1: блоков, занявших %3 мс или дольше: %2 (в пределах допуска)</translation>
    </message>
    <message>
        <source>%1: the last tenth ran at %2 against %3 for the first tenth</source>
        <translation>%1: последняя десятая часть шла на скорости %2 против %3 в первой десятой</translation>
    </message>
    <message>
        <source>%1: at least one block ran at %2 against an average of %3</source>
        <translation>%1: минимум один блок шёл на скорости %2 при средней %3</translation>
    </message>
    <message>
        <source>The run stopped early: %1</source>
        <translation>Прогон остановился раньше времени: %1</translation>
    </message>
    <message>
        <source>Pass %1 write (%2)</source>
        <translation>Проход %1: запись (%2)</translation>
    </message>
    <message>
        <source>random stream %1</source>
        <translation>случайный поток %1</translation>
    </message>
    <message>
        <source>complement of stream %1</source>
        <translation>дополнение потока %1</translation>
    </message>
    <message>
        <source>Pass %1 read-back</source>
        <translation>Проход %1: контрольное чтение</translation>
    </message>
    <message>
        <source>DESTRUCTIVE: data in LBA %1 - %2 was overwritten with random data.</source>
        <translation>ДЕСТРУКТИВНО: данные в LBA %1 – %2 перезаписаны случайными данными.</translation>
    </message>
    <message>
        <source>Skipped block size %1: not a sector multiple, too large, or larger than the region</source>
        <translation>Размер блока %1 пропущен: не кратен сектору, слишком велик или больше области</translation>
    </message>
    <message>
        <source>QD %1</source>
        <translation>QD %1</translation>
    </message>
    <message>
        <source>peak %1 MB/s</source>
        <translation>пик %1 МБ/с</translation>
    </message>
    <message>
        <source>peak %1 IOPS</source>
        <translation>пик %1 IOPS</translation>
    </message>
    <message>
        <source>The finished image could not be read back: %1</source>
        <translation>Готовый образ не удалось прочитать обратно: %1</translation>
    </message>
    <message>
        <source>The OS did not re-read the partition table: %1</source>
        <translation>ОС не перечитала таблицу разделов: %1</translation>
    </message>
    <message>
        <source>%1 unreadable sector(s) were replaced by zeros in the image.</source>
        <translation>Нечитаемых секторов, заменённых в образе нулями: %1.</translation>
    </message>
    <message>
        <source>%1 unreadable source sector(s) were written to the target as zeros.</source>
        <translation>Нечитаемых секторов источника, записанных на цель нулями: %1.</translation>
    </message>
    <message>
        <source>the image checksum is %1, not the expected %2; nothing was written</source>
        <translation>контрольная сумма образа %1, а не ожидаемая %2; ничего не записано</translation>
    </message>
    <message>
        <source>the image ended after %1 of %2 bytes</source>
        <translation>образ закончился после %1 из %2 байт</translation>
    </message>
    <message>
        <source>cannot read back at byte offset %1: %2</source>
        <translation>не удалось прочитать обратно по смещению %1: %2</translation>
    </message>
    <message>
        <source>the output file already exists: %1 (use --force to replace it)</source>
        <translation>выходной файл уже существует: %1 (чтобы заменить его, используйте --force)</translation>
    </message>
    <message>
        <source>the block size must be a multiple of the %1-byte sector size, up to 64 MiB</source>
        <translation>размер блока должен быть кратен размеру сектора (%1 байт), не более 64 МиБ</translation>
    </message>
    <message>
        <source>the source is smaller than the region to copy: %1</source>
        <translation>источник меньше копируемой области: %1</translation>
    </message>
    <message>
        <source>source and target have different logical sector sizes (%1); a sector-for-sector clone is not possible</source>
        <translation>у источника и цели разные размеры логического сектора (%1); посекторное клонирование невозможно</translation>
    </message>
    <message>
        <source>write failed at byte offset %1: %2</source>
        <translation>сбой записи по смещению %1: %2</translation>
    </message>
    <message>
        <source>discard failed at byte offset %1: %2</source>
        <translation>сбой discard по смещению %1: %2</translation>
    </message>
    <message>
        <source>verify read failed at byte offset %1: %2</source>
        <translation>сбой проверочного чтения по смещению %1: %2</translation>
    </message>
    <message>
        <source>read failed at byte offset %1: %2</source>
        <translation>сбой чтения по смещению %1: %2</translation>
    </message>
    <message>
        <source>read failed at LBA %1: %2</source>
        <translation>сбой чтения на LBA %1: %2</translation>
    </message>
    <message>
        <source>device disappeared at LBA %1: %2</source>
        <translation>устройство пропало на LBA %1: %2</translation>
    </message>
    <message>
        <source>device disappeared during the benchmark: %1</source>
        <translation>устройство пропало во время теста: %1</translation>
    </message>
    <message>
        <source>flush after the write phase failed: %1</source>
        <translation>не удалось сбросить буферы после фазы записи: %1</translation>
    </message>
    <message>
        <source>block size %1 must be a multiple of the %2-byte sector size</source>
        <translation>размер блока %1 должен быть кратен размеру сектора (%2 байт)</translation>
    </message>
    <message>
        <source>MBR entry %1 has invalid status byte</source>
        <translation>в записи MBR %1 недопустимый байт состояния</translation>
    </message>
    <message>
        <source>EBR at LBA %1 lies outside the extended partition</source>
        <translation>EBR на LBA %1 лежит вне расширенного раздела</translation>
    </message>
    <message>
        <source>EBR at LBA %1 has no 0x55AA signature</source>
        <translation>у EBR на LBA %1 нет сигнатуры 0x55AA</translation>
    </message>
    <message>
        <source>EBR chain loops back to LBA %1</source>
        <translation>цепочка EBR замыкается на LBA %1</translation>
    </message>
    <message>
        <source>cannot read EBR at LBA %1: %2</source>
        <translation>не удалось прочитать EBR на LBA %1: %2</translation>
    </message>
    <message>
        <source>more than %1 logical partitions; stopped</source>
        <translation>логических разделов больше %1; чтение остановлено</translation>
    </message>
    <message>
        <source>partitions %1 and %2 overlap</source>
        <translation>разделы %1 и %2 пересекаются</translation>
    </message>
    <message>
        <source>no partition %1</source>
        <translation>нет раздела %1</translation>
    </message>
    <message>
        <source>the plan is not valid: %1</source>
        <translation>план недопустим: %1</translation>
    </message>
    <message>
        <source>cannot start an operation in state %1</source>
        <translation>нельзя начать операцию в состоянии %1</translation>
    </message>
    <message>
        <source>cancelled by user</source>
        <translation>отменено пользователем</translation>
    </message>
    <message>
        <source>stopped after %1 %2</source>
        <translation>остановлено после %1 %2</translation>
    </message>
    <message>
        <source>ATA</source>
        <translation>ATA</translation>
    </message>
    <message>
        <source>Access time</source>
        <translation>Время доступа</translation>
    </message>
    <message>
        <source>Administrator</source>
        <translation>Администратор</translation>
    </message>
    <message>
        <source>Apple APFS</source>
        <translation>Apple APFS</translation>
    </message>
    <message>
        <source>Apple HFS+</source>
        <translation>Apple HFS+</translation>
    </message>
    <message>
        <source>Apple HFS/HFS+</source>
        <translation>Apple HFS/HFS+</translation>
    </message>
    <message>
        <source>BENCHMARK</source>
        <translation>ТЕСТ ПРОИЗВОДИТЕЛЬНОСТИ</translation>
    </message>
    <message>
        <source>BIOS boot</source>
        <translation>Загрузочный раздел BIOS</translation>
    </message>
    <message>
        <source>BIOS boot (GPT only)</source>
        <translation>Загрузочный раздел BIOS (только GPT)</translation>
    </message>
    <message>
        <source>Backup boot sector</source>
        <translation>Резервный загрузочный сектор</translation>
    </message>
    <message>
        <source>Backup header LBA</source>
        <translation>LBA резервного заголовка</translation>
    </message>
    <message>
        <source>Base record reference</source>
        <translation>Ссылка на базовую запись</translation>
    </message>
    <message>
        <source>Block size sweep</source>
        <translation>Перебор размеров блока</translation>
    </message>
    <message>
        <source>Blocks count (low)</source>
        <translation>Число блоков (младшая часть)</translation>
    </message>
    <message>
        <source>Blocks per group</source>
        <translation>Блоков в группе</translation>
    </message>
    <message>
        <source>Boot signature</source>
        <translation>Загрузочная сигнатура</translation>
    </message>
    <message>
        <source>Bootstrap code</source>
        <translation>Загрузочный код</translation>
    </message>
    <message>
        <source>Buffered I/O: reads may be served from the OS page cache.</source>
        <translation>Буферизованный ввод-вывод: чтение может обслуживаться из страничного кэша ОС.</translation>
    </message>
    <message>
        <source>Bytes allocated</source>
        <translation>Выделено байт</translation>
    </message>
    <message>
        <source>Bytes in use</source>
        <translation>Используется байт</translation>
    </message>
    <message>
        <source>Bytes per sector</source>
        <translation>Байт в секторе</translation>
    </message>
    <message>
        <source>Bytes per sector shift</source>
        <translation>Сдвиг размера сектора</translation>
    </message>
    <message>
        <source>CANCELLED</source>
        <translation>ОТМЕНЕНО</translation>
    </message>
    <message>
        <source>COMPLETED</source>
        <translation>ЗАВЕРШЕНО</translation>
    </message>
    <message>
        <source>COMPLETED WITH ERRORS</source>
        <translation>ЗАВЕРШЕНО С ОШИБКАМИ</translation>
    </message>
    <message>
        <source>COMPLETED WITH UNREADABLE SECTORS</source>
        <translation>ЗАВЕРШЕНО, ЕСТЬ НЕЧИТАЕМЫЕ СЕКТОРЫ</translation>
    </message>
    <message>
        <source>CREATED</source>
        <translation>СОЗДАНО</translation>
    </message>
    <message>
        <source>CRITICAL</source>
        <translation>КРИТИЧНО</translation>
    </message>
    <message>
        <source>CUSTOM PATTERN</source>
        <translation>СВОЙ ШАБЛОН</translation>
    </message>
    <message>
        <source>Check interval (s)</source>
        <translation>Интервал проверки (с)</translation>
    </message>
    <message>
        <source>Checking the image checksum</source>
        <translation>Проверка контрольной суммы образа</translation>
    </message>
    <message>
        <source>Checksum</source>
        <translation>Контрольная сумма</translation>
    </message>
    <message>
        <source>Cloning</source>
        <translation>Клонирование</translation>
    </message>
    <message>
        <source>Cloning (recovery mode)</source>
        <translation>Клонирование (режим восстановления)</translation>
    </message>
    <message>
        <source>Cluster count</source>
        <translation>Число кластеров</translation>
    </message>
    <message>
        <source>Cluster heap offset (sectors)</source>
        <translation>Смещение кучи кластеров (секторов)</translation>
    </message>
    <message>
        <source>Clusters per MFT record</source>
        <translation>Кластеров на запись MFT</translation>
    </message>
    <message>
        <source>Clusters per group</source>
        <translation>Кластеров в группе</translation>
    </message>
    <message>
        <source>Clusters per index buffer</source>
        <translation>Кластеров на индексный буфер</translation>
    </message>
    <message>
        <source>Compatible features</source>
        <translation>Совместимые возможности</translation>
    </message>
    <message>
        <source>Copying</source>
        <translation>Копирование</translation>
    </message>
    <message>
        <source>Copying (recovery mode)</source>
        <translation>Копирование (режим восстановления)</translation>
    </message>
    <message>
        <source>Creator OS</source>
        <translation>ОС-создатель</translation>
    </message>
    <message>
        <source>Current pending sector count</source>
        <translation>Число секторов, ожидающих переназначения</translation>
    </message>
    <message>
        <source>Custom byte pattern</source>
        <translation>Свой байтовый шаблон</translation>
    </message>
    <message>
        <source>Device reports overall SMART health: FAILED</source>
        <translation>Устройство сообщает общее состояние SMART: СБОЙ</translation>
    </message>
    <message>
        <source>Diagnostics/Recovery</source>
        <translation>Диагностика/восстановление</translation>
    </message>
    <message>
        <source>Direct I/O is not supported for this target; fell back to buffered I/O.</source>
        <translation>Прямой ввод-вывод для этой цели не поддерживается; использован буферизованный.</translation>
    </message>
    <message>
        <source>Direct I/O is unavailable here; writes and verification go through the OS cache.</source>
        <translation>Прямой ввод-вывод здесь недоступен: запись и проверка идут через кэш ОС.</translation>
    </message>
    <message>
        <source>Discard / TRIM (device dependent)</source>
        <translation>Discard / TRIM (зависит от устройства)</translation>
    </message>
    <message>
        <source>Disk GUID</source>
        <translation>GUID диска</translation>
    </message>
    <message>
        <source>Disk signature</source>
        <translation>Сигнатура диска</translation>
    </message>
    <message>
        <source>DoD short</source>
        <translation>DoD (короткий)</translation>
    </message>
    <message>
        <source>Drive number</source>
        <translation>Номер диска</translation>
    </message>
    <message>
        <source>Drive select</source>
        <translation>Выбор диска</translation>
    </message>
    <message>
        <source>EFI System</source>
        <translation>Системный раздел EFI</translation>
    </message>
    <message>
        <source>EFI System Partition</source>
        <translation>Системный раздел EFI (ESP)</translation>
    </message>
    <message>
        <source>ERASED (NOT VERIFIED)</source>
        <translation>СТЁРТО (НЕ ПРОВЕРЕНО)</translation>
    </message>
    <message>
        <source>ERASED AND VERIFIED</source>
        <translation>СТЁРТО И ПРОВЕРЕНО</translation>
    </message>
    <message>
        <source>Empty</source>
        <translation>Пусто</translation>
    </message>
    <message>
        <source>Entries CRC32</source>
        <translation>CRC32 записей</translation>
    </message>
    <message>
        <source>Entries start LBA</source>
        <translation>LBA начала записей</translation>
    </message>
    <message>
        <source>Entry count</source>
        <translation>Число записей</translation>
    </message>
    <message>
        <source>Entry size</source>
        <translation>Размер записи</translation>
    </message>
    <message>
        <source>Errors behaviour</source>
        <translation>Поведение при ошибках</translation>
    </message>
    <message>
        <source>Extended (CHS)</source>
        <translation>Расширенный (CHS)</translation>
    </message>
    <message>
        <source>Extended (LBA)</source>
        <translation>Расширенный (LBA)</translation>
    </message>
    <message>
        <source>Extended boot signature</source>
        <translation>Расширенная загрузочная сигнатура</translation>
    </message>
    <message>
        <source>Extended flags</source>
        <translation>Расширенные флаги</translation>
    </message>
    <message>
        <source>FAIL</source>
        <translation>НЕ ПРОЙДЕНО</translation>
    </message>
    <message>
        <source>FAILED</source>
        <translation>СБОЙ</translation>
    </message>
    <message>
        <source>FAT boot sector</source>
        <translation>Загрузочный сектор FAT</translation>
    </message>
    <message>
        <source>FAT length (sectors)</source>
        <translation>Длина FAT (секторов)</translation>
    </message>
    <message>
        <source>FAT offset (sectors)</source>
        <translation>Смещение FAT (секторов)</translation>
    </message>
    <message>
        <source>FAT12</source>
        <translation>FAT12</translation>
    </message>
    <message>
        <source>FAT12/16</source>
        <translation>FAT12/16</translation>
    </message>
    <message>
        <source>FAT16</source>
        <translation>FAT16</translation>
    </message>
    <message>
        <source>FAT16 (LBA)</source>
        <translation>FAT16 (LBA)</translation>
    </message>
    <message>
        <source>FAT16 &lt;32M</source>
        <translation>FAT16 (&lt;32 МБ)</translation>
    </message>
    <message>
        <source>FAT32</source>
        <translation>FAT32</translation>
    </message>
    <message>
        <source>FAT32 (CHS)</source>
        <translation>FAT32 (CHS)</translation>
    </message>
    <message>
        <source>FAT32 (LBA)</source>
        <translation>FAT32 (LBA)</translation>
    </message>
    <message>
        <source>FAT32 (LBA) / Microsoft basic data</source>
        <translation>FAT32 (LBA) / основные данные Microsoft</translation>
    </message>
    <message>
        <source>FAT32 labels are limited to 11 characters</source>
        <translation>метка FAT32 ограничена 11 символами</translation>
    </message>
    <message>
        <source>FSInfo sector</source>
        <translation>Сектор FSInfo</translation>
    </message>
    <message>
        <source>File system name</source>
        <translation>Название файловой системы</translation>
    </message>
    <message>
        <source>File system revision</source>
        <translation>Версия файловой системы</translation>
    </message>
    <message>
        <source>File system type</source>
        <translation>Тип файловой системы</translation>
    </message>
    <message>
        <source>Final sequential read</source>
        <translation>Итоговое последовательное чтение</translation>
    </message>
    <message>
        <source>First attribute offset</source>
        <translation>Смещение первого атрибута</translation>
    </message>
    <message>
        <source>First data block</source>
        <translation>Первый блок данных</translation>
    </message>
    <message>
        <source>First non-reserved inode</source>
        <translation>Первый незарезервированный inode</translation>
    </message>
    <message>
        <source>First usable LBA</source>
        <translation>Первый пригодный LBA</translation>
    </message>
    <message>
        <source>Flags</source>
        <translation>Флаги</translation>
    </message>
    <message>
        <source>Flushing</source>
        <translation>Сброс буферов</translation>
    </message>
    <message>
        <source>For sensitive data use the drive&apos;s secure-erase / sanitize command or encryption.</source>
        <translation>Для конфиденциальных данных используйте команду безопасного стирания (secure erase / sanitize) накопителя или шифрование.</translation>
    </message>
    <message>
        <source>Free blocks (low)</source>
        <translation>Свободных блоков (младшая часть)</translation>
    </message>
    <message>
        <source>Free inodes</source>
        <translation>Свободных inode</translation>
    </message>
    <message>
        <source>FreeBSD</source>
        <translation>FreeBSD</translation>
    </message>
    <message>
        <source>FreeBSD data</source>
        <translation>Данные FreeBSD</translation>
    </message>
    <message>
        <source>GOOD</source>
        <translation>ХОРОШО</translation>
    </message>
    <message>
        <source>GPT disk lacks a valid protective MBR</source>
        <translation>на GPT-диске нет корректной защитной MBR</translation>
    </message>
    <message>
        <source>GPT has no extended or logical partitions</source>
        <translation>в GPT нет расширенных и логических разделов</translation>
    </message>
    <message>
        <source>GPT header</source>
        <translation>Заголовок GPT</translation>
    </message>
    <message>
        <source>GPT partition entries</source>
        <translation>Записи разделов GPT</translation>
    </message>
    <message>
        <source>GPT partition names are limited to 36 UTF-16 units</source>
        <translation>имена разделов GPT ограничены 36 элементами UTF-16</translation>
    </message>
    <message>
        <source>GPT protective</source>
        <translation>Защитная запись GPT</translation>
    </message>
    <message>
        <source>HDD</source>
        <translation>HDD</translation>
    </message>
    <message>
        <source>Hard link count</source>
        <translation>Число жёстких ссылок</translation>
    </message>
    <message>
        <source>Hashing</source>
        <translation>Вычисление хеша</translation>
    </message>
    <message>
        <source>Hashing and comparing</source>
        <translation>Хеширование и сравнение</translation>
    </message>
    <message>
        <source>Header CRC32</source>
        <translation>CRC32 заголовка</translation>
    </message>
    <message>
        <source>Header size</source>
        <translation>Размер заголовка</translation>
    </message>
    <message>
        <source>Heads</source>
        <translation>Головки</translation>
    </message>
    <message>
        <source>Hidden FAT12</source>
        <translation>Скрытый FAT12</translation>
    </message>
    <message>
        <source>Hidden FAT32</source>
        <translation>Скрытый FAT32</translation>
    </message>
    <message>
        <source>Hidden FAT32 (LBA)</source>
        <translation>Скрытый FAT32 (LBA)</translation>
    </message>
    <message>
        <source>Hidden NTFS</source>
        <translation>Скрытый NTFS</translation>
    </message>
    <message>
        <source>Hidden sectors</source>
        <translation>Скрытые секторы</translation>
    </message>
    <message>
        <source>Hurd</source>
        <translation>Hurd</translation>
    </message>
    <message>
        <source>INCOMPLETE</source>
        <translation>НЕ ЗАВЕРШЕНО</translation>
    </message>
    <message>
        <source>INVALID</source>
        <translation>НЕДОПУСТИМО</translation>
    </message>
    <message>
        <source>INVALID (expected 55 AA)</source>
        <translation>НЕДОПУСТИМО (ожидалось 55 AA)</translation>
    </message>
    <message>
        <source>INVALID (last &lt; first)</source>
        <translation>НЕДОПУСТИМО (последний &lt; первого)</translation>
    </message>
    <message>
        <source>INVALID (must be 0x00 or 0x80)</source>
        <translation>НЕДОПУСТИМО (должно быть 0x00 или 0x80)</translation>
    </message>
    <message>
        <source>Image</source>
        <translation>Образ</translation>
    </message>
    <message>
        <source>Image file: results reflect the host filesystem and OS cache, not a physical device.</source>
        <translation>Файл образа: результаты отражают файловую систему хоста и кэш ОС, а не физическое устройство.</translation>
    </message>
    <message>
        <source>Incompatible features</source>
        <translation>Несовместимые возможности</translation>
    </message>
    <message>
        <source>Inode size</source>
        <translation>Размер inode</translation>
    </message>
    <message>
        <source>Inodes count</source>
        <translation>Число inode</translation>
    </message>
    <message>
        <source>Inodes per group</source>
        <translation>Inode в группе</translation>
    </message>
    <message>
        <source>Jump instruction</source>
        <translation>Команда перехода</translation>
    </message>
    <message>
        <source>LBA range end overflows</source>
        <translation>конец диапазона LBA переполняется</translation>
    </message>
    <message>
        <source>LBA range overflows byte offsets</source>
        <translation>диапазон LBA переполняет байтовые смещения</translation>
    </message>
    <message>
        <source>Last check time</source>
        <translation>Время последней проверки</translation>
    </message>
    <message>
        <source>Last mount time</source>
        <translation>Время последнего монтирования</translation>
    </message>
    <message>
        <source>Last mounted at</source>
        <translation>Последняя точка монтирования</translation>
    </message>
    <message>
        <source>Last usable LBA</source>
        <translation>Последний пригодный LBA</translation>
    </message>
    <message>
        <source>Last write time</source>
        <translation>Время последней записи</translation>
    </message>
    <message>
        <source>Linux</source>
        <translation>Linux</translation>
    </message>
    <message>
        <source>Linux LUKS</source>
        <translation>Linux LUKS</translation>
    </message>
    <message>
        <source>Linux LVM</source>
        <translation>Linux LVM</translation>
    </message>
    <message>
        <source>Linux RAID</source>
        <translation>Linux RAID</translation>
    </message>
    <message>
        <source>Linux extended</source>
        <translation>Linux (расширенный)</translation>
    </message>
    <message>
        <source>Linux extended boot</source>
        <translation>Linux (расширенный загрузочный)</translation>
    </message>
    <message>
        <source>Linux filesystem</source>
        <translation>Файловая система Linux</translation>
    </message>
    <message>
        <source>Linux home</source>
        <translation>Linux (home)</translation>
    </message>
    <message>
        <source>Linux root (ARM64)</source>
        <translation>Linux (корневой, ARM64)</translation>
    </message>
    <message>
        <source>Linux root (x86-64)</source>
        <translation>Linux (корневой, x86-64)</translation>
    </message>
    <message>
        <source>Linux swap</source>
        <translation>Linux (подкачка)</translation>
    </message>
    <message>
        <source>Lites</source>
        <translation>Lites</translation>
    </message>
    <message>
        <source>Log block size</source>
        <translation>Лог. размер блока</translation>
    </message>
    <message>
        <source>Log cluster size</source>
        <translation>Лог. размер кластера</translation>
    </message>
    <message>
        <source>Loop</source>
        <translation>Loop-устройство</translation>
    </message>
    <message>
        <source>MBR can only address the first 2^32 sectors; the rest of the disk cannot be partitioned</source>
        <translation>MBR адресует только первые 2^32 секторов; остальная часть диска не может быть разбита на разделы</translation>
    </message>
    <message>
        <source>MBR contains a GPT protective entry but no GPT was found</source>
        <translation>MBR содержит защитную запись GPT, но GPT не найдена</translation>
    </message>
    <message>
        <source>MBR partition type 0x00 means unused</source>
        <translation>тип раздела MBR 0x00 означает «не используется»</translation>
    </message>
    <message>
        <source>MFT record number</source>
        <translation>Номер записи MFT</translation>
    </message>
    <message>
        <source>MFT record size field is out of range</source>
        <translation>поле размера записи MFT вне допустимых пределов</translation>
    </message>
    <message>
        <source>MMC</source>
        <translation>MMC</translation>
    </message>
    <message>
        <source>Magic</source>
        <translation>Сигнатура (magic)</translation>
    </message>
    <message>
        <source>Masix</source>
        <translation>Masix</translation>
    </message>
    <message>
        <source>Master / Extended Boot Record</source>
        <translation>Главная / расширенная загрузочная запись</translation>
    </message>
    <message>
        <source>Max mount count</source>
        <translation>Макс. число монтирований</translation>
    </message>
    <message>
        <source>Media descriptor</source>
        <translation>Дескриптор носителя</translation>
    </message>
    <message>
        <source>Microsoft basic data</source>
        <translation>Основные данные Microsoft</translation>
    </message>
    <message>
        <source>Microsoft reserved</source>
        <translation>Зарезервировано Microsoft</translation>
    </message>
    <message>
        <source>Microsoft reserved (GPT only)</source>
        <translation>Зарезервировано Microsoft (только GPT)</translation>
    </message>
    <message>
        <source>Minor revision</source>
        <translation>Дополнительная версия</translation>
    </message>
    <message>
        <source>Mount count</source>
        <translation>Число монтирований</translation>
    </message>
    <message>
        <source>Must be zero</source>
        <translation>Должно быть нулём</translation>
    </message>
    <message>
        <source>NO NAME</source>
        <translation>NO NAME</translation>
    </message>
    <message>
        <source>NOT ZERO</source>
        <translation>НЕ НОЛЬ</translation>
    </message>
    <message>
        <source>NTFS</source>
        <translation>NTFS</translation>
    </message>
    <message>
        <source>NTFS / Microsoft basic data</source>
        <translation>NTFS / основные данные Microsoft</translation>
    </message>
    <message>
        <source>NTFS MFT record header</source>
        <translation>Заголовок записи MFT (NTFS)</translation>
    </message>
    <message>
        <source>NTFS boot sector</source>
        <translation>Загрузочный сектор NTFS</translation>
    </message>
    <message>
        <source>NTFS/exFAT/HPFS</source>
        <translation>NTFS/exFAT/HPFS</translation>
    </message>
    <message>
        <source>NVMe</source>
        <translation>NVMe</translation>
    </message>
    <message>
        <source>NVMe media errors</source>
        <translation>Ошибки носителя NVMe</translation>
    </message>
    <message>
        <source>Next attribute id</source>
        <translation>Следующий идентификатор атрибута</translation>
    </message>
    <message>
        <source>No SMART health data was available</source>
        <translation>Данные о состоянии SMART недоступны</translation>
    </message>
    <message>
        <source>Number of FATs</source>
        <translation>Число таблиц FAT</translation>
    </message>
    <message>
        <source>OEM ID</source>
        <translation>Идентификатор OEM</translation>
    </message>
    <message>
        <source>OEM name</source>
        <translation>Название OEM</translation>
    </message>
    <message>
        <source>Offline uncorrectable count</source>
        <translation>Число неисправимых секторов (offline)</translation>
    </message>
    <message>
        <source>One fill, 0xFF (1 pass)</source>
        <translation>Заполнение 0xFF (1 проход)</translation>
    </message>
    <message>
        <source>PASS</source>
        <translation>ПРОЙДЕНО</translation>
    </message>
    <message>
        <source>PASS WITH WARNINGS</source>
        <translation>ПРОЙДЕНО С ПРЕДУПРЕЖДЕНИЯМИ</translation>
    </message>
    <message>
        <source>PAUSED</source>
        <translation>ПАУЗА</translation>
    </message>
    <message>
        <source>Partition offset (sectors)</source>
        <translation>Смещение раздела (секторов)</translation>
    </message>
    <message>
        <source>Pass 1 write (random)</source>
        <translation>Проход 1: запись (случайные данные)</translation>
    </message>
    <message>
        <source>Percent in use</source>
        <translation>Процент использования</translation>
    </message>
    <message>
        <source>Queue depth sweep</source>
        <translation>Перебор глубины очереди</translation>
    </message>
    <message>
        <source>Quick format: the data area was not erased; old files may still be recoverable. Use ERASE first if that matters.</source>
        <translation>Быстрое форматирование: область данных не стиралась, старые файлы могут быть восстановимы. Если это важно, сначала выполните СТИРАНИЕ.</translation>
    </message>
    <message>
        <source>RAID/DM</source>
        <translation>RAID/DM</translation>
    </message>
    <message>
        <source>READ</source>
        <translation>ЧТЕНИЕ</translation>
    </message>
    <message>
        <source>READ scans never use a writable authorization</source>
        <translation>сканирование READ никогда не использует разрешение на запись</translation>
    </message>
    <message>
        <source>READY</source>
        <translation>ГОТОВО</translation>
    </message>
    <message>
        <source>RUNNING</source>
        <translation>ВЫПОЛНЯЕТСЯ</translation>
    </message>
    <message>
        <source>Random</source>
        <translation>Случайное</translation>
    </message>
    <message>
        <source>Random data (1 pass)</source>
        <translation>Случайные данные (1 проход)</translation>
    </message>
    <message>
        <source>Random data (N passes)</source>
        <translation>Случайные данные (N проходов)</translation>
    </message>
    <message>
        <source>Random read</source>
        <translation>Случайное чтение</translation>
    </message>
    <message>
        <source>Random read sampling</source>
        <translation>Выборочное случайное чтение</translation>
    </message>
    <message>
        <source>Random reads confined to a region under 2 GiB may be served from the drive&apos;s cache.</source>
        <translation>Случайное чтение в области менее 2 ГиБ может обслуживаться из кэша накопителя.</translation>
    </message>
    <message>
        <source>Random write</source>
        <translation>Случайная запись</translation>
    </message>
    <message>
        <source>Read speed curve</source>
        <translation>Кривая скорости чтения</translation>
    </message>
    <message>
        <source>Read-only compatible features</source>
        <translation>Возможности, совместимые только для чтения</translation>
    </message>
    <message>
        <source>Reallocated sector count</source>
        <translation>Число переназначенных секторов</translation>
    </message>
    <message>
        <source>Reported uncorrectable errors</source>
        <translation>Сообщённые неисправимые ошибки</translation>
    </message>
    <message>
        <source>Reserved</source>
        <translation>Зарезервировано</translation>
    </message>
    <message>
        <source>Reserved blocks (low)</source>
        <translation>Зарезервировано блоков (младшая часть)</translation>
    </message>
    <message>
        <source>Reserved sectors</source>
        <translation>Зарезервированные секторы</translation>
    </message>
    <message>
        <source>Revision</source>
        <translation>Версия</translation>
    </message>
    <message>
        <source>Revision level</source>
        <translation>Уровень версии</translation>
    </message>
    <message>
        <source>Root directory cluster</source>
        <translation>Кластер корневого каталога</translation>
    </message>
    <message>
        <source>Root directory entries</source>
        <translation>Записей в корневом каталоге</translation>
    </message>
    <message>
        <source>SANITIZE</source>
        <translation>SANITIZE</translation>
    </message>
    <message>
        <source>SCSI</source>
        <translation>SCSI</translation>
    </message>
    <message>
        <source>SMART</source>
        <translation>SMART</translation>
    </message>
    <message>
        <source>SMART counters did not change during the run</source>
        <translation>Счётчики SMART за время проверки не изменились</translation>
    </message>
    <message>
        <source>SMART is not available through this connection</source>
        <translation>SMART недоступен через это подключение</translation>
    </message>
    <message>
        <source>SMART is supported but disabled on the device</source>
        <translation>SMART поддерживается, но отключён на устройстве</translation>
    </message>
    <message>
        <source>SMART overall health reports FAILED after the run</source>
        <translation>После проверки общее состояние SMART — СБОЙ</translation>
    </message>
    <message>
        <source>SMART was not compared (no readable snapshot before and after); only what the run itself measured counts</source>
        <translation>SMART не сравнивался (нет читаемых снимков до и после); учитывается только то, что измерено самим прогоном</translation>
    </message>
    <message>
        <source>SSD</source>
        <translation>SSD</translation>
    </message>
    <message>
        <source>STOPPING</source>
        <translation>ОСТАНОВКА</translation>
    </message>
    <message>
        <source>Sectors per FAT (16-bit)</source>
        <translation>Секторов на FAT (16 бит)</translation>
    </message>
    <message>
        <source>Sectors per FAT (32-bit)</source>
        <translation>Секторов на FAT (32 бита)</translation>
    </message>
    <message>
        <source>Sectors per cluster</source>
        <translation>Секторов в кластере</translation>
    </message>
    <message>
        <source>Sectors per cluster shift</source>
        <translation>Сдвиг секторов в кластере</translation>
    </message>
    <message>
        <source>Sectors per track</source>
        <translation>Секторов на дорожку</translation>
    </message>
    <message>
        <source>Sequence number</source>
        <translation>Порядковый номер</translation>
    </message>
    <message>
        <source>Sequential</source>
        <translation>Последовательное</translation>
    </message>
    <message>
        <source>Sequential read</source>
        <translation>Последовательное чтение</translation>
    </message>
    <message>
        <source>Sequential write</source>
        <translation>Последовательная запись</translation>
    </message>
    <message>
        <source>Signature</source>
        <translation>Сигнатура</translation>
    </message>
    <message>
        <source>Small test size: a large part of it may be served from the drive&apos;s own cache.</source>
        <translation>Малый объём теста: значительная его часть может обслуживаться из собственного кэша накопителя.</translation>
    </message>
    <message>
        <source>Speed curve</source>
        <translation>Кривая скорости</translation>
    </message>
    <message>
        <source>State</source>
        <translation>Состояние</translation>
    </message>
    <message>
        <source>TRIM</source>
        <translation>TRIM</translation>
    </message>
    <message>
        <source>Target is an image file, not a physical device.</source>
        <translation>Целью является файл образа, а не физическое устройство.</translation>
    </message>
    <message>
        <source>The confirmed range is larger than the image; the extra sectors are not written.</source>
        <translation>Подтверждённый диапазон больше образа; лишние секторы не записываются.</translation>
    </message>
    <message>
        <source>The data read back from the target differs from the image (mismatches are reported per block).</source>
        <translation>Данные, прочитанные с цели, отличаются от образа (расхождения указываются по блокам).</translation>
    </message>
    <message>
        <source>The device exposes neither a serial number nor a stable id; an identical drive could not be told apart.</source>
        <translation>Устройство не сообщает ни серийный номер, ни устойчивый идентификатор: такой же накопитель нельзя будет отличить от этого.</translation>
    </message>
    <message>
        <source>The drive&apos;s volatile write cache can inflate short write tests; each phase ends with a flush.</source>
        <translation>Энергозависимый кэш записи накопителя может завышать результат коротких тестов записи; каждая фаза заканчивается сбросом кэша.</translation>
    </message>
    <message>
        <source>The finished image does not match the data that was read: do not rely on it.</source>
        <translation>Готовый образ не совпадает с прочитанными данными: полагаться на него нельзя.</translation>
    </message>
    <message>
        <source>The image extends past the end of the compared device.</source>
        <translation>Образ выходит за конец сравниваемого устройства.</translation>
    </message>
    <message>
        <source>The run was cancelled before every stage finished</source>
        <translation>Прогон отменён до завершения всех этапов</translation>
    </message>
    <message>
        <source>The target does not read back as the source did (mismatches are reported per block).</source>
        <translation>Цель читается иначе, чем источник (расхождения указываются по блокам).</translation>
    </message>
    <message>
        <source>The target is larger than the copied region; the rest of it was not touched (partitions and file systems are not resized).</source>
        <translation>Цель больше скопированной области; остальная её часть не затронута (разделы и файловые системы не изменяются в размере).</translation>
    </message>
    <message>
        <source>This header LBA</source>
        <translation>LBA этого заголовка</translation>
    </message>
    <message>
        <source>This is a removable device: check it is the one you mean.</source>
        <translation>Это съёмное устройство: убедитесь, что выбрано именно то, что нужно.</translation>
    </message>
    <message>
        <source>To confirm, type the model name</source>
        <translation>Для подтверждения введите название модели</translation>
    </message>
    <message>
        <source>Total sectors</source>
        <translation>Всего секторов</translation>
    </message>
    <message>
        <source>Total sectors (16-bit)</source>
        <translation>Всего секторов (16 бит)</translation>
    </message>
    <message>
        <source>Total sectors (32-bit)</source>
        <translation>Всего секторов (32 бита)</translation>
    </message>
    <message>
        <source>UDMA CRC error count</source>
        <translation>Число ошибок UDMA CRC</translation>
    </message>
    <message>
        <source>UNKNOWN</source>
        <translation>НЕИЗВЕСТНО</translation>
    </message>
    <message>
        <source>USB</source>
        <translation>USB</translation>
    </message>
    <message>
        <source>UUID</source>
        <translation>UUID</translation>
    </message>
    <message>
        <source>Unknown</source>
        <translation>Неизвестно</translation>
    </message>
    <message>
        <source>Unused</source>
        <translation>Не используется</translation>
    </message>
    <message>
        <source>Update sequence entries</source>
        <translation>Записей массива обновления</translation>
    </message>
    <message>
        <source>Update sequence offset</source>
        <translation>Смещение массива обновления</translation>
    </message>
    <message>
        <source>VALIDATING</source>
        <translation>ПРОВЕРКА УСЛОВИЙ</translation>
    </message>
    <message>
        <source>VERIFICATION FAILED</source>
        <translation>ПРОВЕРКА НЕ ПРОЙДЕНА</translation>
    </message>
    <message>
        <source>VERIFY</source>
        <translation>ПРОВЕРКА</translation>
    </message>
    <message>
        <source>VERIFY scans need a device-side verify command, which this build does not issue</source>
        <translation>для проверок VERIFY нужна команда проверки на стороне устройства, которую эта сборка не отправляет</translation>
    </message>
    <message>
        <source>Verifying</source>
        <translation>Проверка</translation>
    </message>
    <message>
        <source>Verifying the image file</source>
        <translation>Проверка файла образа</translation>
    </message>
    <message>
        <source>Version</source>
        <translation>Версия</translation>
    </message>
    <message>
        <source>VirtIO</source>
        <translation>VirtIO</translation>
    </message>
    <message>
        <source>Volume ID</source>
        <translation>Идентификатор тома</translation>
    </message>
    <message>
        <source>Volume flags</source>
        <translation>Флаги тома</translation>
    </message>
    <message>
        <source>Volume label</source>
        <translation>Метка тома</translation>
    </message>
    <message>
        <source>Volume length (sectors)</source>
        <translation>Длина тома (секторов)</translation>
    </message>
    <message>
        <source>Volume name</source>
        <translation>Имя тома</translation>
    </message>
    <message>
        <source>Volume serial number</source>
        <translation>Серийный номер тома</translation>
    </message>
    <message>
        <source>WARNING</source>
        <translation>ПРЕДУПРЕЖДЕНИЕ</translation>
    </message>
    <message>
        <source>WRITE</source>
        <translation>ЗАПИСЬ</translation>
    </message>
    <message>
        <source>WRITE+READBACK</source>
        <translation>ЗАПИСЬ + КОНТРОЛЬНОЕ ЧТЕНИЕ</translation>
    </message>
    <message>
        <source>Windows Dynamic</source>
        <translation>Динамический диск Windows</translation>
    </message>
    <message>
        <source>Windows LDM data</source>
        <translation>Данные LDM Windows</translation>
    </message>
    <message>
        <source>Windows LDM metadata</source>
        <translation>Метаданные LDM Windows</translation>
    </message>
    <message>
        <source>Windows Recovery</source>
        <translation>Среда восстановления Windows</translation>
    </message>
    <message>
        <source>Windows Storage Spaces</source>
        <translation>Дисковые пространства Windows</translation>
    </message>
    <message>
        <source>Write queue depth above 1 uses several threads on one exclusive handle (Windows serializes these).</source>
        <translation>Глубина очереди записи больше 1 использует несколько потоков на одном эксклюзивном дескрипторе (Windows упорядочивает их).</translation>
    </message>
    <message>
        <source>Writes went through the OS cache; the data was flushed at the end of each phase.</source>
        <translation>Запись шла через кэш ОС; данные сбрасывались в конце каждой фазы.</translation>
    </message>
    <message>
        <source>Writing</source>
        <translation>Запись</translation>
    </message>
    <message>
        <source>Writing file system metadata</source>
        <translation>Запись метаданных файловой системы</translation>
    </message>
    <message>
        <source>Yes</source>
        <translation>Да</translation>
    </message>
    <message>
        <source>Zero fill (1 pass)</source>
        <translation>Заполнение нулями (1 проход)</translation>
    </message>
    <message>
        <source>a FAT32 volume can have at most 2^32 sectors</source>
        <translation>том FAT32 может содержать не более 2^32 секторов</translation>
    </message>
    <message>
        <source>a clone needs exactly one confirmed range</source>
        <translation>для клонирования нужен ровно один подтверждённый диапазон</translation>
    </message>
    <message>
        <source>a format needs exactly one confirmed range (the volume)</source>
        <translation>для форматирования нужен ровно один подтверждённый диапазон (том)</translation>
    </message>
    <message>
        <source>a restore needs exactly one confirmed range</source>
        <translation>для восстановления нужен ровно один подтверждённый диапазон</translation>
    </message>
    <message>
        <source>aborted verification: too many unreadable or mismatching sectors</source>
        <translation>проверка прервана: слишком много нечитаемых или несовпадающих секторов</translation>
    </message>
    <message>
        <source>aborted: too many unreadable or mismatching sectors</source>
        <translation>прервано: слишком много нечитаемых или несовпадающих секторов</translation>
    </message>
    <message>
        <source>all zero</source>
        <translation>всё нули</translation>
    </message>
    <message>
        <source>an MBR disk can have only one extended partition</source>
        <translation>на диске MBR может быть только один расширенный раздел</translation>
    </message>
    <message>
        <source>an MBR disk holds at most four primary partitions</source>
        <translation>на диске MBR не более четырёх основных разделов</translation>
    </message>
    <message>
        <source>an authorized target can only run write benchmarks</source>
        <translation>авторизованная цель допускает только тесты записи</translation>
    </message>
    <message>
        <source>an empty LBA range was requested</source>
        <translation>запрошен пустой диапазон LBA</translation>
    </message>
    <message>
        <source>an extended partition cannot be bootable</source>
        <translation>расширенный раздел не может быть загрузочным</translation>
    </message>
    <message>
        <source>attributes are a GPT concept</source>
        <translation>атрибуты — понятие GPT</translation>
    </message>
    <message>
        <source>backup GPT header is missing</source>
        <translation>отсутствует резервный заголовок GPT</translation>
    </message>
    <message>
        <source>block size is larger than the test region</source>
        <translation>размер блока больше области теста</translation>
    </message>
    <message>
        <source>block size must be a multiple of the I/O alignment</source>
        <translation>размер блока должен быть кратен выравниванию ввода-вывода</translation>
    </message>
    <message>
        <source>block size must be a multiple of the sector size, at most 64 MiB</source>
        <translation>размер блока должен быть кратен размеру сектора и не более 64 МиБ</translation>
    </message>
    <message>
        <source>boot signature is not 55 AA</source>
        <translation>загрузочная сигнатура не 55 AA</translation>
    </message>
    <message>
        <source>buffered I/O</source>
        <translation>буферизованный ввод-вывод</translation>
    </message>
    <message>
        <source>bytes in use exceed bytes allocated</source>
        <translation>используемых байт больше, чем выделено</translation>
    </message>
    <message>
        <source>cancelled before start</source>
        <translation>отменено до начала</translation>
    </message>
    <message>
        <source>cannot convert between extended and ordinary partitions; delete and re-create</source>
        <translation>нельзя преобразовать расширенный раздел в обычный и наоборот; удалите и создайте заново</translation>
    </message>
    <message>
        <source>cannot flush</source>
        <translation>не удалось сбросить буферы</translation>
    </message>
    <message>
        <source>cannot read the image file</source>
        <translation>не удалось прочитать файл образа</translation>
    </message>
    <message>
        <source>cannot write</source>
        <translation>не удалось записать</translation>
    </message>
    <message>
        <source>choose mbr or gpt</source>
        <translation>выберите mbr или gpt</translation>
    </message>
    <message>
        <source>cloning needs a read-only source handle</source>
        <translation>клонирование требует дескриптор источника только для чтения</translation>
    </message>
    <message>
        <source>cluster size would exceed 32 MiB</source>
        <translation>размер кластера превысил бы 32 МиБ</translation>
    </message>
    <message>
        <source>comparison needs a read-only handle</source>
        <translation>сравнение требует дескриптор только для чтения</translation>
    </message>
    <message>
        <source>completed with errors: see the report</source>
        <translation>завершено с ошибками: см. отчёт</translation>
    </message>
    <message>
        <source>completed with errors: some sectors could not be written or did not verify</source>
        <translation>завершено с ошибками: часть секторов не удалось записать или они не прошли проверку</translation>
    </message>
    <message>
        <source>create an extended partition first</source>
        <translation>сначала создайте расширенный раздел</translation>
    </message>
    <message>
        <source>curve points must be between 2 and 10000</source>
        <translation>число точек кривой должно быть от 2 до 10000</translation>
    </message>
    <message>
        <source>custom pattern mode needs a pattern</source>
        <translation>режим своего шаблона требует шаблон</translation>
    </message>
    <message>
        <source>deleted file</source>
        <translation>удалённый файл</translation>
    </message>
    <message>
        <source>device has no sector size</source>
        <translation>у устройства нет размера сектора</translation>
    </message>
    <message>
        <source>device is too small for a GPT</source>
        <translation>устройство слишком мало для GPT</translation>
    </message>
    <message>
        <source>device is too small for an MBR</source>
        <translation>устройство слишком мало для MBR</translation>
    </message>
    <message>
        <source>device reports zero capacity</source>
        <translation>устройство сообщает нулевую ёмкость</translation>
    </message>
    <message>
        <source>direct I/O</source>
        <translation>прямой ввод-вывод</translation>
    </message>
    <message>
        <source>direct I/O is not in use; read-back may be served from the OS cache and hide bad sectors</source>
        <translation>прямой ввод-вывод не используется; контрольное чтение может обслуживаться из кэша ОС и скрыть сбойные секторы</translation>
    </message>
    <message>
        <source>direct I/O is not supported here; using buffered I/O (results may reflect the OS cache)</source>
        <translation>прямой ввод-вывод здесь не поддерживается; используется буферизованный (результаты могут отражать кэш ОС)</translation>
    </message>
    <message>
        <source>direct I/O unavailable, buffered</source>
        <translation>прямой ввод-вывод недоступен, буферизованный</translation>
    </message>
    <message>
        <source>discard (TRIM)</source>
        <translation>discard (TRIM)</translation>
    </message>
    <message>
        <source>discard is not supported by this backend</source>
        <translation>discard не поддерживается этим модулем</translation>
    </message>
    <message>
        <source>empty (no boot code)</source>
        <translation>пусто (нет загрузочного кода)</translation>
    </message>
    <message>
        <source>empty LBA range</source>
        <translation>пустой диапазон LBA</translation>
    </message>
    <message>
        <source>empty search pattern</source>
        <translation>пустой шаблон поиска</translation>
    </message>
    <message>
        <source>end is before start</source>
        <translation>конец раньше начала</translation>
    </message>
    <message>
        <source>entry size below 128 is invalid</source>
        <translation>размер записи меньше 128 недопустим</translation>
    </message>
    <message>
        <source>entry size is not a power of two</source>
        <translation>размер записи не является степенью двойки</translation>
    </message>
    <message>
        <source>exFAT</source>
        <translation>exFAT</translation>
    </message>
    <message>
        <source>exFAT / Microsoft basic data</source>
        <translation>exFAT / основные данные Microsoft</translation>
    </message>
    <message>
        <source>exFAT boot sector</source>
        <translation>Загрузочный сектор exFAT</translation>
    </message>
    <message>
        <source>exFAT labels are limited to 11 characters</source>
        <translation>метка exFAT ограничена 11 символами</translation>
    </message>
    <message>
        <source>ext2/3/4 superblock</source>
        <translation>Суперблок ext2/3/4</translation>
    </message>
    <message>
        <source>extension of another record</source>
        <translation>расширение другой записи</translation>
    </message>
    <message>
        <source>header CRC32 does not match</source>
        <translation>CRC32 заголовка не совпадает</translation>
    </message>
    <message>
        <source>header size is outside 92..sector size; CRC not checked</source>
        <translation>размер заголовка вне диапазона 92..размер сектора; CRC не проверялся</translation>
    </message>
    <message>
        <source>image already finished</source>
        <translation>образ уже завершён</translation>
    </message>
    <message>
        <source>image creation</source>
        <translation>создание образа</translation>
    </message>
    <message>
        <source>image restore</source>
        <translation>восстановление образа</translation>
    </message>
    <message>
        <source>image verification</source>
        <translation>проверка образа</translation>
    </message>
    <message>
        <source>imaging needs a read-only source handle</source>
        <translation>создание образа требует дескриптор источника только для чтения</translation>
    </message>
    <message>
        <source>labelled FAT12/16 but the cluster count is 65525 or more</source>
        <translation>помечена как FAT12/16, но кластеров 65525 или больше</translation>
    </message>
    <message>
        <source>labelled FAT32 but the cluster count is below 65525</source>
        <translation>помечена как FAT32, но кластеров меньше 65525</translation>
    </message>
    <message>
        <source>latency thresholds must be increasing</source>
        <translation>пороги задержки должны возрастать</translation>
    </message>
    <message>
        <source>logical partitions need an extended partition</source>
        <translation>логическим разделам нужен расширенный раздел</translation>
    </message>
    <message>
        <source>max_errors must be at least 1 with the stop-after-N policy</source>
        <translation>max_errors должно быть не меньше 1 при политике «остановиться после N»</translation>
    </message>
    <message>
        <source>metadata read too large</source>
        <translation>слишком большой объём метаданных для чтения</translation>
    </message>
    <message>
        <source>more data than the announced image size</source>
        <translation>данных больше объявленного размера образа</translation>
    </message>
    <message>
        <source>more partitions than GPT entries</source>
        <translation>разделов больше, чем записей GPT</translation>
    </message>
    <message>
        <source>more than four primary partitions</source>
        <translation>более четырёх основных разделов</translation>
    </message>
    <message>
        <source>more than one extended partition</source>
        <translation>более одного расширенного раздела</translation>
    </message>
    <message>
        <source>more than one extended partition in MBR</source>
        <translation>в MBR более одного расширенного раздела</translation>
    </message>
    <message>
        <source>needs_recovery: the journal has not been replayed (unclean shutdown)</source>
        <translation>needs_recovery: журнал не воспроизведён (нештатное выключение)</translation>
    </message>
    <message>
        <source>no block sizes to test</source>
        <translation>нет размеров блока для проверки</translation>
    </message>
    <message>
        <source>no device</source>
        <translation>нет устройства</translation>
    </message>
    <message>
        <source>no device opener</source>
        <translation>не задан способ открытия устройства</translation>
    </message>
    <message>
        <source>no output file given</source>
        <translation>не указан выходной файл</translation>
    </message>
    <message>
        <source>no partition table in the plan</source>
        <translation>в плане нет таблицы разделов</translation>
    </message>
    <message>
        <source>no passes to run</source>
        <translation>нет проходов для выполнения</translation>
    </message>
    <message>
        <source>no queue depths to test</source>
        <translation>нет глубин очереди для проверки</translation>
    </message>
    <message>
        <source>no source device</source>
        <translation>не указано устройство-источник</translation>
    </message>
    <message>
        <source>no usable GPT header</source>
        <translation>нет пригодного заголовка GPT</translation>
    </message>
    <message>
        <source>no valid GPT copy found; partition entries were not trusted</source>
        <translation>корректная копия GPT не найдена; записи разделов не считаются достоверными</translation>
    </message>
    <message>
        <source>none of the block sizes fit this device</source>
        <translation>ни один из размеров блока не подходит для этого устройства</translation>
    </message>
    <message>
        <source>not a GPT table</source>
        <translation>это не таблица GPT</translation>
    </message>
    <message>
        <source>not an MBR table</source>
        <translation>это не таблица MBR</translation>
    </message>
    <message>
        <source>not available</source>
        <translation>недоступно</translation>
    </message>
    <message>
        <source>not checked</source>
        <translation>не проверено</translation>
    </message>
    <message>
        <source>nothing to look for</source>
        <translation>нечего искать</translation>
    </message>
    <message>
        <source>odd number of hex digits</source>
        <translation>нечётное число шестнадцатеричных цифр</translation>
    </message>
    <message>
        <source>offline uncorrectable sectors</source>
        <translation>Неисправимые секторы (offline)</translation>
    </message>
    <message>
        <source>offset + length overflows 64 bits</source>
        <translation>смещение + длина превышает 64 бита</translation>
    </message>
    <message>
        <source>ok (55 AA)</source>
        <translation>в порядке (55 AA)</translation>
    </message>
    <message>
        <source>ok (EF53)</source>
        <translation>в порядке (EF53)</translation>
    </message>
    <message>
        <source>only GPT partitions have names</source>
        <translation>имена есть только у разделов GPT</translation>
    </message>
    <message>
        <source>only wildcards</source>
        <translation>только подстановочные знаки</translation>
    </message>
    <message>
        <source>operation already validated</source>
        <translation>операция уже проверена</translation>
    </message>
    <message>
        <source>partition extends past 2^64 sectors</source>
        <translation>раздел выходит за 2^64 секторов</translation>
    </message>
    <message>
        <source>physical devices are not supported on this platform</source>
        <translation>физические устройства на этой платформе не поддерживаются</translation>
    </message>
    <message>
        <source>primary GPT is damaged; partitions were read from the backup GPT</source>
        <translation>основная GPT повреждена; разделы прочитаны из резервной GPT</translation>
    </message>
    <message>
        <source>queue depth must be between 1 and 64</source>
        <translation>глубина очереди должна быть от 1 до 64</translation>
    </message>
    <message>
        <source>queue depths must be between 1 and 64</source>
        <translation>глубины очереди должны быть от 1 до 64</translation>
    </message>
    <message>
        <source>random data</source>
        <translation>случайные данные</translation>
    </message>
    <message>
        <source>random passes must be between 1 and 99</source>
        <translation>число случайных проходов должно быть от 1 до 99</translation>
    </message>
    <message>
        <source>random reads</source>
        <translation>случайных чтений</translation>
    </message>
    <message>
        <source>range covers the entire 64-bit LBA space</source>
        <translation>диапазон охватывает всё 64-битное пространство LBA</translation>
    </message>
    <message>
        <source>range size overflows</source>
        <translation>размер диапазона переполняется</translation>
    </message>
    <message>
        <source>re-reading partitions is not supported by this backend</source>
        <translation>повторное чтение таблицы разделов не поддерживается этим модулем</translation>
    </message>
    <message>
        <source>read benchmarks require a read-only device handle</source>
        <translation>тесты чтения требуют дескриптор устройства только для чтения</translation>
    </message>
    <message>
        <source>reallocated sectors</source>
        <translation>Переназначенные секторы</translation>
    </message>
    <message>
        <source>reallocation events</source>
        <translation>События переназначения</translation>
    </message>
    <message>
        <source>reported uncorrectable errors</source>
        <translation>Сообщённые неисправимые ошибки</translation>
    </message>
    <message>
        <source>sample percentage must be in (0, 100]</source>
        <translation>процент выборки должен быть в диапазоне (0, 100]</translation>
    </message>
    <message>
        <source>sector size is zero</source>
        <translation>размер сектора равен нулю</translation>
    </message>
    <message>
        <source>sector size must be 512 or 4096</source>
        <translation>размер сектора должен быть 512 или 4096</translation>
    </message>
    <message>
        <source>sectors pending reallocation</source>
        <translation>Секторы, ожидающие переназначения</translation>
    </message>
    <message>
        <source>sectors per cluster is not a power of two</source>
        <translation>число секторов в кластере не является степенью двойки</translation>
    </message>
    <message>
        <source>slow fractions must satisfy 0 &lt;= warn &lt; fail &lt;= 1</source>
        <translation>доли медленных блоков должны удовлетворять 0 &lt;= предупреждение &lt; сбой &lt;= 1</translation>
    </message>
    <message>
        <source>smartctl JSON root is not an object</source>
        <translation>корень JSON smartctl не является объектом</translation>
    </message>
    <message>
        <source>smartctl not found; install smartmontools or set PLATTER_SMARTCTL</source>
        <translation>smartctl не найден; установите smartmontools или задайте PLATTER_SMARTCTL</translation>
    </message>
    <message>
        <source>source and target are the same device</source>
        <translation>источник и цель — одно и то же устройство</translation>
    </message>
    <message>
        <source>spin-up retries</source>
        <translation>Повторные раскрутки шпинделя</translation>
    </message>
    <message>
        <source>surface scan requires a read-only device handle</source>
        <translation>проверка поверхности требует дескриптор только для чтения</translation>
    </message>
    <message>
        <source>test region holds no complete block</source>
        <translation>в области теста нет ни одного полного блока</translation>
    </message>
    <message>
        <source>text is not valid UTF-8</source>
        <translation>текст не является корректным UTF-8</translation>
    </message>
    <message>
        <source>the 53 bytes after the name must be zero on exFAT</source>
        <translation>53 байта после имени в exFAT должны быть нулевыми</translation>
    </message>
    <message>
        <source>the GPT entry array is full</source>
        <translation>массив записей GPT заполнен</translation>
    </message>
    <message>
        <source>the all-zero type means unused</source>
        <translation>нулевой тип означает «не используется»</translation>
    </message>
    <message>
        <source>the authorization was not granted for a clone</source>
        <translation>разрешение на клонирование не выдано</translation>
    </message>
    <message>
        <source>the authorization was not granted for a partition edit</source>
        <translation>разрешение на изменение разделов не выдано</translation>
    </message>
    <message>
        <source>the authorization was not granted for a qualification run</source>
        <translation>разрешение на квалификационный прогон не выдано</translation>
    </message>
    <message>
        <source>the authorization was not granted for a write test</source>
        <translation>разрешение на тест записи не выдано</translation>
    </message>
    <message>
        <source>the authorization was not granted for an erase</source>
        <translation>разрешение на стирание не выдано</translation>
    </message>
    <message>
        <source>the authorization was not granted for creating a file system</source>
        <translation>разрешение на создание файловой системы не выдано</translation>
    </message>
    <message>
        <source>the authorization was not granted for restoring an image</source>
        <translation>разрешение на восстановление образа не выдано</translation>
    </message>
    <message>
        <source>the block size must be a multiple of the I/O alignment</source>
        <translation>размер блока должен быть кратен выравниванию ввода-вывода</translation>
    </message>
    <message>
        <source>the block size must be between 1 byte and 64 MiB</source>
        <translation>размер блока должен быть от 1 байта до 64 МиБ</translation>
    </message>
    <message>
        <source>the bootable flag is an MBR concept (GPT: attribute bit 2)</source>
        <translation>флаг загрузочности — понятие MBR (в GPT: бит атрибута 2)</translation>
    </message>
    <message>
        <source>the compressed image does not record its size (not made by platter?)</source>
        <translation>в сжатом образе не записан его размер (создан не platter?)</translation>
    </message>
    <message>
        <source>the compressed image is truncated</source>
        <translation>сжатый образ усечён</translation>
    </message>
    <message>
        <source>the confirmation text does not match</source>
        <translation>введённый текст подтверждения не совпадает</translation>
    </message>
    <message>
        <source>the confirmed range no longer matches the device</source>
        <translation>подтверждённый диапазон больше не соответствует устройству</translation>
    </message>
    <message>
        <source>the device geometry differs from the one the plan was made for</source>
        <translation>геометрия устройства отличается от той, для которой составлялся план</translation>
    </message>
    <message>
        <source>the device has no partition table; create one first</source>
        <translation>на устройстве нет таблицы разделов; сначала создайте её</translation>
    </message>
    <message>
        <source>the device is read-only (hardware switch or OS flag)</source>
        <translation>устройство доступно только для чтения (аппаратный переключатель или флаг ОС)</translation>
    </message>
    <message>
        <source>the device is too small for a GPT</source>
        <translation>устройство слишком мало для GPT</translation>
    </message>
    <message>
        <source>the device reports no capacity</source>
        <translation>устройство не сообщает свою ёмкость</translation>
    </message>
    <message>
        <source>the device reports no sector size</source>
        <translation>устройство не сообщает размер сектора</translation>
    </message>
    <message>
        <source>the exFAT label contains a character that is not allowed</source>
        <translation>метка exFAT содержит недопустимый символ</translation>
    </message>
    <message>
        <source>the expected checksum must be 64 hex digits</source>
        <translation>ожидаемая контрольная сумма должна состоять из 64 шестнадцатеричных цифр</translation>
    </message>
    <message>
        <source>the file system is flagged as having errors</source>
        <translation>файловая система помечена как содержащая ошибки</translation>
    </message>
    <message>
        <source>the file system was not cleanly unmounted</source>
        <translation>файловая система не была корректно отмонтирована</translation>
    </message>
    <message>
        <source>the image file is the target</source>
        <translation>файл образа является целью</translation>
    </message>
    <message>
        <source>the image is empty</source>
        <translation>образ пуст</translation>
    </message>
    <message>
        <source>the output file is the source device</source>
        <translation>выходной файл — это устройство-источник</translation>
    </message>
    <message>
        <source>the pattern must be 1 to 4096 bytes</source>
        <translation>шаблон должен занимать от 1 до 4096 байт</translation>
    </message>
    <message>
        <source>the pattern must be at most 4096 bytes</source>
        <translation>шаблон должен занимать не более 4096 байт</translation>
    </message>
    <message>
        <source>the real values are in the update sequence array</source>
        <translation>настоящие значения находятся в массиве обновления</translation>
    </message>
    <message>
        <source>the reserved area and FATs are larger than the volume</source>
        <translation>зарезервированная область и таблицы FAT больше тома</translation>
    </message>
    <message>
        <source>the scan range extends outside the range that was confirmed</source>
        <translation>диапазон проверки выходит за пределы подтверждённого</translation>
    </message>
    <message>
        <source>the slow-request limits must be positive</source>
        <translation>пороги медленных запросов должны быть положительными</translation>
    </message>
    <message>
        <source>the source reports zero capacity</source>
        <translation>источник сообщает нулевую ёмкость</translation>
    </message>
    <message>
        <source>the test region is not inside the range that was confirmed for writing</source>
        <translation>область теста не лежит внутри диапазона, подтверждённого для записи</translation>
    </message>
    <message>
        <source>the volume is too small for FAT32</source>
        <translation>том слишком мал для FAT32</translation>
    </message>
    <message>
        <source>the volume is too small for FAT32 (it needs at least 65525 clusters; about 33 MiB with 512-byte clusters)</source>
        <translation>том слишком мал для FAT32 (нужно не менее 65525 кластеров; около 33 МиБ при кластерах по 512 байт)</translation>
    </message>
    <message>
        <source>the volume is too small for exFAT</source>
        <translation>том слишком мал для exFAT</translation>
    </message>
    <message>
        <source>the volume is too small for exFAT (minimum 1 MiB)</source>
        <translation>том слишком мал для exFAT (минимум 1 МиБ)</translation>
    </message>
    <message>
        <source>the volume is too small for exFAT with this cluster size</source>
        <translation>том слишком мал для exFAT с таким размером кластера</translation>
    </message>
    <message>
        <source>the zstd level must be between 1 and 19</source>
        <translation>уровень zstd должен быть от 1 до 19</translation>
    </message>
    <message>
        <source>this build has no zstd support</source>
        <translation>эта сборка не поддерживает zstd</translation>
    </message>
    <message>
        <source>this is the system disk</source>
        <translation>это системный диск</translation>
    </message>
    <message>
        <source>too many clusters for FAT32; use a larger cluster size</source>
        <translation>слишком много кластеров для FAT32; используйте больший размер кластера</translation>
    </message>
    <message>
        <source>too many clusters for exFAT; use a larger cluster size</source>
        <translation>слишком много кластеров для exFAT; используйте больший размер кластера</translation>
    </message>
    <message>
        <source>type 0x00 means unused</source>
        <translation>тип 0x00 означает «не используется»</translation>
    </message>
    <message>
        <source>type 0xEE: protective MBR, the real table is GPT</source>
        <translation>тип 0xEE: защитная MBR, настоящая таблица — GPT</translation>
    </message>
    <message>
        <source>unknown session kind</source>
        <translation>неизвестный вид сеанса</translation>
    </message>
    <message>
        <source>unknown structure</source>
        <translation>неизвестная структура</translation>
    </message>
    <message>
        <source>unsupported I/O alignment</source>
        <translation>неподдерживаемое выравнивание ввода-вывода</translation>
    </message>
    <message>
        <source>unusually large entry count</source>
        <translation>необычно большое число записей</translation>
    </message>
    <message>
        <source>update sequence array lies outside the supplied bytes or overlaps the header</source>
        <translation>массив обновления лежит вне переданных байт или пересекается с заголовком</translation>
    </message>
    <message>
        <source>use the extended flag to create an extended partition</source>
        <translation>чтобы создать расширенный раздел, используйте флаг «расширенный»</translation>
    </message>
    <message>
        <source>verified, identical</source>
        <translation>проверено, совпадает</translation>
    </message>
    <message>
        <source>worker handle is not read-only</source>
        <translation>рабочий дескриптор не является дескриптором только для чтения</translation>
    </message>
    <message>
        <source>write benchmarks need an authorized target (typed confirmation)</source>
        <translation>тесты записи требуют авторизованной цели (подтверждение вводом текста)</translation>
    </message>
    <message>
        <source>write passes must be between 1 and 4</source>
        <translation>число проходов записи должно быть от 1 до 4</translation>
    </message>
    <message>
        <source>write phase without an authorized target</source>
        <translation>фаза записи без авторизованной цели</translation>
    </message>
    <message>
        <source>writing is not available for this device (permissions or read-only media)</source>
        <translation>запись на это устройство недоступна (права доступа или носитель только для чтения)</translation>
    </message>
    <message>
        <source>yes (metadata read back)</source>
        <translation>да (метаданные прочитаны обратно)</translation>
    </message>
    <message>
        <source>os error</source>
        <translation>код ОС</translation>
    </message>
    <message>
        <source>Flash storage remaps and over-provisions blocks, so overwriting cannot reach every cell. For sensitive data use the drive&apos;s secure-erase / sanitize command or encryption.</source>
        <translation>Флеш-память переназначает блоки и держит резервную область, поэтому перезапись не может достичь каждой ячейки. Для конфиденциальных данных используйте команду безопасного стирания (secure erase / sanitize) накопителя или шифрование.</translation>
    </message>
    <message>
        <source>No adverse observations in the available SMART data. This describes the current state only and does not predict future reliability.</source>
        <translation>В доступных данных SMART тревожных признаков нет. Это описывает только текущее состояние и не предсказывает надёжность в будущем.</translation>
    </message>
    <message>
        <source>SMART reports PASSED, but no attribute or health-log data was available to confirm it</source>
        <translation>SMART сообщает PASSED, но данных атрибутов или журнала состояния для подтверждения нет</translation>
    </message>
    <message>
        <source>PARTITION_EDIT</source>
        <translation>ПРАВКА РАЗДЕЛОВ</translation>
    </message>
    <message>
        <source>POWER_MANAGEMENT</source>
        <translation>УПРАВЛЕНИЕ ПИТАНИЕМ</translation>
    </message>
    <message>
        <source>RAW_SECTOR_ACCESS</source>
        <translation>ПРЯМОЙ ДОСТУП К СЕКТОРАМ</translation>
    </message>
    <message>
        <source>SECURE_ERASE</source>
        <translation>БЕЗОПАСНОЕ СТИРАНИЕ</translation>
    </message>
    <message>
        <source>SELF_TEST</source>
        <translation>САМОТЕСТ</translation>
    </message>
    <message>
        <source>Unknown (0x%1)</source>
        <translation>Неизвестно (0x%1)</translation>
    </message>
    <message>
        <source>No</source>
        <translation>Нет</translation>
    </message>
    <message>
        <source>Filename</source>
        <translation>Имя файла</translation>
    </message>
    <message>
        <source>Raw disk image</source>
        <translation>Образ диска (сырой)</translation>
    </message>
    <message>
        <source>buffer too small for sectors</source>
        <translation>буфер слишком мал для секторов</translation>
    </message>
    <message>
        <source>device was opened read-only</source>
        <translation>устройство открыто только для чтения</translation>
    </message>
    <message>
        <source>discard is only implemented on Linux</source>
        <translation>discard реализован только в Linux</translation>
    </message>
    <message>
        <source>discard range lies outside the device</source>
        <translation>диапазон discard выходит за пределы устройства</translation>
    </message>
    <message>
        <source>image sector size must be 512 or 4096</source>
        <translation>размер сектора образа должен быть 512 или 4096</translation>
    </message>
    <message>
        <source>raw block devices are only supported on Linux and Windows</source>
        <translation>блочные устройства напрямую поддерживаются только в Linux и Windows</translation>
    </message>
    <message>
        <source>re-reading partitions is only implemented on Linux</source>
        <translation>повторное чтение таблицы разделов реализовано только в Linux</translation>
    </message>
    <message>
        <source>sector request overflows</source>
        <translation>запрос секторов переполняется</translation>
    </message>
    <message>
        <source>%1 GPT header at LBA %2: invalid header size %3</source>
        <translation>Заголовок GPT (%1) на LBA %2: недопустимый размер заголовка %3</translation>
    </message>
    <message>
        <source>%1 GPT header at LBA %2: header CRC32 mismatch</source>
        <translation>Заголовок GPT (%1) на LBA %2: CRC32 заголовка не совпадает</translation>
    </message>
    <message>
        <source>%1 GPT header at LBA %2: header claims to be at LBA %3</source>
        <translation>Заголовок GPT (%1) на LBA %2: заголовок утверждает, что находится на LBA %3</translation>
    </message>
    <message>
        <source>%1 GPT header at LBA %2: invalid entry size %3</source>
        <translation>Заголовок GPT (%1) на LBA %2: недопустимый размер записи %3</translation>
    </message>
    <message>
        <source>%1 GPT header at LBA %2: implausible entry array (%3 x %4)</source>
        <translation>Заголовок GPT (%1) на LBA %2: неправдоподобный массив записей (%3 × %4)</translation>
    </message>
    <message>
        <source>%1 GPT header at LBA %2: cannot read entry array: %3</source>
        <translation>Заголовок GPT (%1) на LBA %2: не удалось прочитать массив записей: %3</translation>
    </message>
    <message>
        <source>%1 GPT header at LBA %2: partition entry array CRC32 mismatch</source>
        <translation>Заголовок GPT (%1) на LBA %2: CRC32 массива записей разделов не совпадает</translation>
    </message>
    <message>
        <source>primary GPT points to backup at LBA %1 but the device ends at LBA %2</source>
        <translation>основная GPT указывает на резервную копию на LBA %1, но устройство заканчивается на LBA %2</translation>
    </message>
    <message>
        <source>sector 0 holds a %1 volume boot record, not a partition table (unpartitioned device)</source>
        <translation>в секторе 0 находится загрузочная запись тома %1, а не таблица разделов (устройство без разделов)</translation>
    </message>
    <message>
        <source>partition %1 starts at LBA 0</source>
        <translation>раздел %1 начинается с LBA 0</translation>
    </message>
    <message>
        <source>partition %1 extends past the end of the device (last LBA %2, device has %3)</source>
        <translation>раздел %1 выходит за конец устройства (последний LBA %2, на устройстве %3)</translation>
    </message>
    <message>
        <source>partition %1 lies outside the GPT usable area</source>
        <translation>раздел %1 лежит вне пригодной области GPT</translation>
    </message>
    <message>
        <source>partition %1 ends before it starts</source>
        <translation>раздел %1 заканчивается раньше, чем начинается</translation>
    </message>
    <message>
        <source>partition %1 lies outside the usable area (LBA %2 - %3)</source>
        <translation>раздел %1 лежит вне пригодной области (LBA %2 – %3)</translation>
    </message>
    <message>
        <source>partition %1 is beyond what an MBR can address (2^32 sectors)</source>
        <translation>раздел %1 выходит за пределы адресации MBR (2^32 секторов)</translation>
    </message>
    <message>
        <source>partition %1 has no type</source>
        <translation>у раздела %1 не задан тип</translation>
    </message>
    <message>
        <source>partition %1 overlaps partition %2</source>
        <translation>раздел %1 пересекается с разделом %2</translation>
    </message>
    <message>
        <source>partition %1 shares its unique GUID with another partition</source>
        <translation>у раздела %1 тот же уникальный GUID, что и у другого раздела</translation>
    </message>
    <message>
        <source>partition %1 does not fit inside the extended partition (it needs one free sector before it for its EBR)</source>
        <translation>раздел %1 не помещается внутри расширенного раздела (перед ним нужен один свободный сектор для EBR)</translation>
    </message>
    <message>
        <source>partition %1 overlaps or abuts partition %2 (an EBR sector must fit between them)</source>
        <translation>раздел %1 пересекается с разделом %2 или вплотную прилегает к нему (между ними должен поместиться сектор EBR)</translation>
    </message>
    <message>
        <source>partition %1 is not aligned to 1 MiB</source>
        <translation>раздел %1 не выровнен по 1 МиБ</translation>
    </message>
    <message>
        <source>GPT entry size %1 is not supported for editing</source>
        <translation>размер записи GPT %1 не поддерживается для редактирования</translation>
    </message>
    <message>
        <source>GPT with %1 entries is not supported for editing</source>
        <translation>GPT с числом записей %1 не поддерживается для редактирования</translation>
    </message>
    <message>
        <source>Entry %1 status</source>
        <translation>Запись %1: статус</translation>
    </message>
    <message>
        <source>Entry %1 first CHS</source>
        <translation>Запись %1: первый CHS</translation>
    </message>
    <message>
        <source>Entry %1 type</source>
        <translation>Запись %1: тип</translation>
    </message>
    <message>
        <source>Entry %1 last CHS</source>
        <translation>Запись %1: последний CHS</translation>
    </message>
    <message>
        <source>Entry %1 first LBA</source>
        <translation>Запись %1: первый LBA</translation>
    </message>
    <message>
        <source>Entry %1 sectors</source>
        <translation>Запись %1: секторов</translation>
    </message>
    <message>
        <source>Entry %1 unique GUID</source>
        <translation>Запись %1: уникальный GUID</translation>
    </message>
    <message>
        <source>Entry %1 last LBA</source>
        <translation>Запись %1: последний LBA</translation>
    </message>
    <message>
        <source>Entry %1 attributes</source>
        <translation>Запись %1: атрибуты</translation>
    </message>
    <message>
        <source>Entry %1 name</source>
        <translation>Запись %1: имя</translation>
    </message>
    <message>
        <source>%1 at 512 B/sector</source>
        <translation>%1 при секторе 512 Б</translation>
    </message>
    <message>
        <source>entry %1 has an invalid status byte %2</source>
        <translation>в записи %1 недопустимый байт состояния %2</translation>
    </message>
    <message>
        <source>entry %1 has a type but no sectors</source>
        <translation>у записи %1 есть тип, но нет секторов</translation>
    </message>
    <message>
        <source>entry %1 starts before the previous entry ends (overlap or out of order)</source>
        <translation>запись %1 начинается раньше конца предыдущей (пересечение или нарушен порядок)</translation>
    </message>
    <message>
        <source>Entry %1: last LBA is before first LBA</source>
        <translation>Запись %1: последний LBA раньше первого</translation>
    </message>
    <message>
        <source>all %1 entries in view are empty</source>
        <translation>все записи в поле зрения (%1) пусты</translation>
    </message>
    <message>
        <source>%1 of %2 entries in view are in use (empty entries are not listed)</source>
        <translation>используется записей: %1 из %2 в поле зрения (пустые записи не перечислены)</translation>
    </message>
    <message>
        <source>bytes per sector %1 is not a power of two between 512 and 4096</source>
        <translation>число байт в секторе %1 не является степенью двойки от 512 до 4096</translation>
    </message>
    <message>
        <source>bytes-per-sector shift %1 is outside 9..12</source>
        <translation>сдвиг размера сектора %1 вне диапазона 9..12</translation>
    </message>
    <message>
        <source>derived: %1 data clusters of %2 =&gt; %3 by the Microsoft cluster-count rule, volume %4</source>
        <translation>вычислено: кластеров данных %1 по %2 =&gt; %3 по правилу Microsoft о числе кластеров, том %4</translation>
    </message>
    <message>
        <source>derived: first FAT at sector %1, data area at sector %2</source>
        <translation>вычислено: первая FAT в секторе %1, область данных с сектора %2</translation>
    </message>
    <message>
        <source>derived: sector %1, cluster %2, volume %3</source>
        <translation>вычислено: сектор %1, кластер %2, том %3</translation>
    </message>
    <message>
        <source>derived: cluster %1, volume %2</source>
        <translation>вычислено: кластер %1, том %2</translation>
    </message>
    <message>
        <source>derived: $MFT begins at byte %1 (sector %2) of the volume</source>
        <translation>вычислено: $MFT начинается с байта %1 (сектор %2) тома</translation>
    </message>
    <message>
        <source>derived: MFT record size %1</source>
        <translation>вычислено: размер записи MFT %1</translation>
    </message>
    <message>
        <source>derived: looks like %1, %2 blocks of %3 = %4</source>
        <translation>вычислено: похоже на %1, блоков: %2 по %3 = %4</translation>
    </message>
    <message>
        <source>derived: %1 block groups</source>
        <translation>вычислено: групп блоков: %1</translation>
    </message>
    <message>
        <source>the last two bytes of each 512-byte stride hold the update sequence number on disk; the real values are in the update sequence array</source>
        <translation>последние два байта каждого 512-байтового шага на диске содержат номер обновления; настоящие значения находятся в массиве обновления</translation>
    </message>
    <message>
        <source>%1 is a directory</source>
        <translation>%1 — это каталог</translation>
    </message>
    <message>
        <source>%1 is zstd-compressed but this build has no zstd support</source>
        <translation>%1 сжат zstd, но эта сборка не поддерживает zstd</translation>
    </message>
    <message>
        <source>%1 is held exclusively by a destructive operation</source>
        <translation>%1 занято исключительно деструктивной операцией</translation>
    </message>
    <message>
        <source>no block device named &apos;%1&apos;</source>
        <translation>нет блочного устройства с именем «%1»</translation>
    </message>
    <message>
        <source>invalid block device name &apos;%1&apos;</source>
        <translation>недопустимое имя блочного устройства «%1»</translation>
    </message>
    <message>
        <source>backup</source>
        <translation>резервный</translation>
    </message>
    <message>
        <source>bootable</source>
        <translation>загрузочный</translation>
    </message>
    <message>
        <source>primary</source>
        <translation>основной</translation>
    </message>
    <message>
        <source>volumes are mounted on this device (%1) and the dismount was not acknowledged</source>
        <translation>на этом устройстве смонтированы тома (%1), а отмонтирование не подтверждено</translation>
    </message>
    <message>
        <source>a volume was mounted at %1 after the preview was shown</source>
        <translation>том смонтирован в %1 уже после показа предпросмотра</translation>
    </message>
</context>
<context>
    <name>platter::gui::BenchmarkTab</name>
    <message>
        <source>&amp;Start</source>
        <translation>&amp;Старт</translation>
    </message>
    <message>
        <source>S&amp;top</source>
        <translation>С&amp;топ</translation>
    </message>
    <message>
        <source>&amp;Export...</source>
        <translation>&amp;Экспорт...</translation>
    </message>
    <message>
        <source>Benchmark Settings</source>
        <translation>Параметры теста</translation>
    </message>
    <message>
        <source>Test:</source>
        <translation>Тест:</translation>
    </message>
    <message>
        <source>Default</source>
        <translation>По умолчанию</translation>
    </message>
    <message>
        <source>Block size:</source>
        <translation>Размер блока:</translation>
    </message>
    <message>
        <source>Queue depth: worker threads, each with its own handle</source>
        <translation>Глубина очереди: рабочие потоки, у каждого свой дескриптор</translation>
    </message>
    <message>
        <source>Queue depth:</source>
        <translation>Глубина очереди:</translation>
    </message>
    <message>
        <source> s</source>
        <translation> с</translation>
    </message>
    <message>
        <source>Time limit:</source>
        <translation>Предел по времени:</translation>
    </message>
    <message>
        <source>Size limit:</source>
        <translation>Предел по объёму:</translation>
    </message>
    <message>
        <source>Curve points:</source>
        <translation>Точек кривой:</translation>
    </message>
    <message>
        <source>Per point:</source>
        <translation>На точку:</translation>
    </message>
    <message>
        <source>Sizes:</source>
        <translation>Размеры:</translation>
    </message>
    <message>
        <source>Depths:</source>
        <translation>Глубины:</translation>
    </message>
    <message>
        <source>end of device</source>
        <translation>конец устройства</translation>
    </message>
    <message>
        <source>Start LBA:</source>
        <translation>Начальный LBA:</translation>
    </message>
    <message>
        <source>End LBA:</source>
        <translation>Конечный LBA:</translation>
    </message>
    <message>
        <source>Direct I/O (bypass OS cache)</source>
        <translation>Прямой ввод-вывод (в обход кэша ОС)</translation>
    </message>
    <message>
        <source>Read tests are read-only. Write tests DESTROY data in the LBA range given here (both ends required) and ask for confirmation. Throughput = bytes / wall time; latency = per request.</source>
        <translation>Тесты чтения только читают. Тесты записи УНИЧТОЖАЮТ данные в указанном здесь диапазоне LBA (нужны оба конца) и запрашивают подтверждение. Пропускная способность = байты / реальное время; задержка — на запрос.</translation>
    </message>
    <message>
        <source>-</source>
        <translation>-</translation>
    </message>
    <message>
        <source>Chart:</source>
        <translation>График:</translation>
    </message>
    <message>
        <source>Compare:</source>
        <translation>Сравнить:</translation>
    </message>
    <message>
        <source>Phase</source>
        <translation>Фаза</translation>
    </message>
    <message>
        <source>Block</source>
        <translation>Блок</translation>
    </message>
    <message>
        <source>QD</source>
        <translation>QD</translation>
    </message>
    <message>
        <source>MB/s</source>
        <translation>МБ/с</translation>
    </message>
    <message>
        <source>MiB/s</source>
        <translation>МиБ/с</translation>
    </message>
    <message>
        <source>IOPS</source>
        <translation>IOPS</translation>
    </message>
    <message>
        <source>Mean ms</source>
        <translation>Средн., мс</translation>
    </message>
    <message>
        <source>P50 ms</source>
        <translation>P50, мс</translation>
    </message>
    <message>
        <source>P95 ms</source>
        <translation>P95, мс</translation>
    </message>
    <message>
        <source>P99 ms</source>
        <translation>P99, мс</translation>
    </message>
    <message>
        <source>Max ms</source>
        <translation>Макс., мс</translation>
    </message>
    <message>
        <source>Errors</source>
        <translation>Ошибки</translation>
    </message>
    <message>
        <source>Results</source>
        <translation>Результаты</translation>
    </message>
    <message>
        <source>#</source>
        <translation>№</translation>
    </message>
    <message>
        <source>Started (UTC)</source>
        <translation>Начало (UTC)</translation>
    </message>
    <message>
        <source>Test</source>
        <translation>Тест</translation>
    </message>
    <message>
        <source>Result</source>
        <translation>Результат</translation>
    </message>
    <message>
        <source>State</source>
        <translation>Состояние</translation>
    </message>
    <message>
        <source>&amp;Load</source>
        <translation>&amp;Загрузить</translation>
    </message>
    <message>
        <source>&amp;Delete</source>
        <translation>&amp;Удалить</translation>
    </message>
    <message>
        <source>History</source>
        <translation>История</translation>
    </message>
    <message>
        <source>end (%1)</source>
        <translation>конец (%1)</translation>
    </message>
    <message>
        <source>Ready: %1 (read-only).</source>
        <translation>Готово: %1 (только чтение).</translation>
    </message>
    <message>
        <source>No device selected.</source>
        <translation>Устройство не выбрано.</translation>
    </message>
    <message>
        <source>Start LBA is not a number.</source>
        <translation>Начальный LBA не является числом.</translation>
    </message>
    <message>
        <source>End LBA is not a number.</source>
        <translation>Конечный LBA не является числом.</translation>
    </message>
    <message>
        <source>Sizes must look like 4K,64K,1M.</source>
        <translation>Размеры должны выглядеть так: 4K,64K,1M.</translation>
    </message>
    <message>
        <source>Depths must look like 1,4,16,32.</source>
        <translation>Глубины должны выглядеть так: 1,4,16,32.</translation>
    </message>
    <message>
        <source>Benchmark</source>
        <translation>Тест производительности</translation>
    </message>
    <message>
        <source>A write benchmark needs an explicit region. Enter both Start LBA and End LBA.</source>
        <translation>Тест записи требует явно заданной области. Укажите начальный и конечный LBA.</translation>
    </message>
    <message>
        <source>Confirm Write Benchmark</source>
        <translation>Подтверждение теста записи</translation>
    </message>
    <message>
        <source>%1 benchmark</source>
        <translation>Тест: %1</translation>
    </message>
    <message>
        <source>Write benchmark refused: %1</source>
        <translation>Тест записи отклонён: %1</translation>
    </message>
    <message>
        <source>Write benchmark authorized (destroys data in the chosen range)</source>
        <translation>Тест записи разрешён (уничтожает данные в выбранном диапазоне)</translation>
    </message>
    <message>
        <source>Cannot open device for benchmarking: %1</source>
        <translation>Не удаётся открыть устройство для теста: %1</translation>
    </message>
    <message>
        <source>Cannot open %1 for reading:
%2%3</source>
        <translation>Не удаётся открыть %1 для чтения:
%2%3</translation>
    </message>
    <message>
        <source>

Benchmarking a physical disk needs root / Administrator.</source>
        <translation>

Для тестирования физического диска нужны права root / администратора.</translation>
    </message>
    <message>
        <source>Invalid benchmark settings: %1</source>
        <translation>Недопустимые параметры теста: %1</translation>
    </message>
    <message>
        <source>%1 started (block %2, QD %3, %4)</source>
        <translation>%1: запущен (блок %2, QD %3, %4)</translation>
    </message>
    <message>
        <source>direct I/O</source>
        <translation>прямой ввод-вывод</translation>
    </message>
    <message>
        <source>buffered I/O</source>
        <translation>буферизованный ввод-вывод</translation>
    </message>
    <message>
        <source>Stopping...</source>
        <translation>Остановка...</translation>
    </message>
    <message>
        <source>%1 on %2 ...  %3 %   %4 MB/s</source>
        <translation>%1 на %2 ...  %3 %   %4 МБ/с</translation>
    </message>
    <message>
        <source>%1: %2</source>
        <translation>%1: %2</translation>
    </message>
    <message>
        <source>%1 finished: %2</source>
        <translation>%1 завершён: %2</translation>
    </message>
    <message>
        <source>Saved as #%1</source>
        <translation>Сохранено как №%1</translation>
    </message>
    <message>
        <source>Benchmark not saved to history: %1</source>
        <translation>Тест не сохранён в истории: %1</translation>
    </message>
    <message>
        <source>No caveats.</source>
        <translation>Оговорок нет.</translation>
    </message>
    <message>
        <source>Throughput vs time</source>
        <translation>Пропускная способность по времени</translation>
    </message>
    <message>
        <source>Throughput vs position</source>
        <translation>Пропускная способность по положению</translation>
    </message>
    <message>
        <source>IOPS vs time</source>
        <translation>IOPS по времени</translation>
    </message>
    <message>
        <source>Read speed vs position</source>
        <translation>Скорость чтения по положению</translation>
    </message>
    <message>
        <source>Throughput by block size</source>
        <translation>Пропускная способность по размеру блока</translation>
    </message>
    <message>
        <source>IOPS by block size</source>
        <translation>IOPS по размеру блока</translation>
    </message>
    <message>
        <source>IOPS by queue depth</source>
        <translation>IOPS по глубине очереди</translation>
    </message>
    <message>
        <source>Mean latency by queue depth</source>
        <translation>Средняя задержка по глубине очереди</translation>
    </message>
    <message>
        <source>Latency histogram</source>
        <translation>Гистограмма задержек</translation>
    </message>
    <message>
        <source>Run a benchmark or load one from History</source>
        <translation>Запустите тест или загрузите его из истории</translation>
    </message>
    <message>
        <source>#%1 %2</source>
        <translation>№%1 %2</translation>
    </message>
    <message>
        <source>Throughput over time</source>
        <translation>Пропускная способность во времени</translation>
    </message>
    <message>
        <source>seconds</source>
        <translation>секунды</translation>
    </message>
    <message>
        <source>IOPS over time</source>
        <translation>IOPS во времени</translation>
    </message>
    <message>
        <source>position (% of device)</source>
        <translation>положение (% устройства)</translation>
    </message>
    <message>
        <source>ms</source>
        <translation>мс</translation>
    </message>
    <message>
        <source>Latency distribution</source>
        <translation>Распределение задержек</translation>
    </message>
    <message>
        <source>% of requests</source>
        <translation>% запросов</translation>
    </message>
    <message>
        <source>(none)</source>
        <translation>(нет)</translation>
    </message>
    <message>
        <source>History is disabled.</source>
        <translation>История отключена.</translation>
    </message>
    <message numerus="yes">
        <source>%n saved session(s) for this device</source>
        <translation>
            <numerusform>Сохранённый сеанс для этого устройства: %n</numerusform>
            <numerusform>Сохранённых сеанса для этого устройства: %n</numerusform>
            <numerusform>Сохранённых сеансов для этого устройства: %n</numerusform>
        </translation>
    </message>
    <message>
        <source>Viewing saved session #%1 from %2</source>
        <translation>Просмотр сохранённого сеанса №%1 от %2</translation>
    </message>
    <message>
        <source>Delete Session</source>
        <translation>Удаление сеанса</translation>
    </message>
    <message>
        <source>Delete saved benchmark session #%1?</source>
        <translation>Удалить сохранённый сеанс теста №%1?</translation>
    </message>
    <message>
        <source>platter benchmark, JSON (*.json)</source>
        <translation>Тест platter, JSON (*.json)</translation>
    </message>
    <message>
        <source>Text report (*.txt)</source>
        <translation>Текстовый отчёт (*.txt)</translation>
    </message>
    <message>
        <source>Export Benchmark</source>
        <translation>Экспорт теста</translation>
    </message>
    <message>
        <source>Cannot write %1:
%2</source>
        <translation>Не удаётся записать %1:
%2</translation>
    </message>
    <message>
        <source>Benchmark exported to %1 (serial numbers omitted)</source>
        <translation>Тест экспортирован в %1 (серийные номера не включены)</translation>
    </message>
</context>
<context>
    <name>platter::gui::Chart</name>
    <message>
        <source>No results yet</source>
        <translation>Результатов пока нет</translation>
    </message>
    <message>
        <source>No data</source>
        <translation>Нет данных</translation>
    </message>
    <message>
        <source>this run</source>
        <translation>этот прогон</translation>
    </message>
</context>
<context>
    <name>platter::gui::ChecksumDialog</name>
    <message>
        <source>Checksum Calculator</source>
        <translation>Контрольная сумма</translation>
    </message>
    <message>
        <source>&amp;Browse...</source>
        <translation>&amp;Обзор...</translation>
    </message>
    <message>
        <source>File:</source>
        <translation>Файл:</translation>
    </message>
    <message>
        <source>optional: paste the SHA-256 you expect</source>
        <translation>необязательно: вставьте ожидаемый SHA-256</translation>
    </message>
    <message>
        <source>Expected:</source>
        <translation>Ожидается:</translation>
    </message>
    <message>
        <source>SHA-256:</source>
        <translation>SHA-256:</translation>
    </message>
    <message>
        <source>Choose a file and press Calculate.</source>
        <translation>Выберите файл и нажмите «Вычислить».</translation>
    </message>
    <message>
        <source>&amp;Calculate</source>
        <translation>&amp;Вычислить</translation>
    </message>
    <message>
        <source>&amp;Stop</source>
        <translation>О&amp;становить</translation>
    </message>
    <message>
        <source>Co&amp;py</source>
        <translation>&amp;Копировать</translation>
    </message>
    <message>
        <source>C&amp;lose</source>
        <translation>&amp;Закрыть</translation>
    </message>
    <message>
        <source>Choose a file</source>
        <translation>Выберите файл</translation>
    </message>
    <message>
        <source>Cannot open the file: %1</source>
        <translation>Не удаётся открыть файл: %1</translation>
    </message>
    <message>
        <source>Reading %1 bytes...</source>
        <translation>Чтение, байт: %1...</translation>
    </message>
    <message>
        <source>Stopped.</source>
        <translation>Остановлено.</translation>
    </message>
    <message>
        <source>Read error after %1 bytes: %2</source>
        <translation>Ошибка чтения после %1 байт: %2</translation>
    </message>
    <message>
        <source>Done: %1 bytes.</source>
        <translation>Готово: %1 байт.</translation>
    </message>
    <message>
        <source>&lt;b&gt;MATCH&lt;/b&gt;: the file has the expected checksum.</source>
        <translation>&lt;b&gt;СОВПАДАЕТ&lt;/b&gt;: у файла ожидаемая контрольная сумма.</translation>
    </message>
    <message>
        <source>&lt;b&gt;MISMATCH&lt;/b&gt;: the file does NOT have the expected checksum.</source>
        <translation>&lt;b&gt;НЕ СОВПАДАЕТ&lt;/b&gt;: у файла НЕ та контрольная сумма, которая ожидалась.</translation>
    </message>
</context>
<context>
    <name>platter::gui::EraseTab</name>
    <message>
        <source>&amp;Erase...</source>
        <translation>&amp;Стереть...</translation>
    </message>
    <message>
        <source>S&amp;top</source>
        <translation>С&amp;топ</translation>
    </message>
    <message>
        <source>&amp;Save Report...</source>
        <translation>&amp;Сохранить отчёт...</translation>
    </message>
    <message>
        <source>Erase Settings</source>
        <translation>Параметры стирания</translation>
    </message>
    <message>
        <source>Method:</source>
        <translation>Метод:</translation>
    </message>
    <message>
        <source>Random passes:</source>
        <translation>Случайных проходов:</translation>
    </message>
    <message>
        <source>hex bytes, e.g. DEADBEEF</source>
        <translation>байты в hex, например DEADBEEF</translation>
    </message>
    <message>
        <source>Pattern:</source>
        <translation>Шаблон:</translation>
    </message>
    <message>
        <source>Read back last pass</source>
        <translation>Читать обратно последний проход</translation>
    </message>
    <message>
        <source>Read back every pass</source>
        <translation>Читать обратно каждый проход</translation>
    </message>
    <message>
        <source>Do not verify</source>
        <translation>Не проверять</translation>
    </message>
    <message>
        <source>Verification:</source>
        <translation>Проверка:</translation>
    </message>
    <message>
        <source>0 (whole device)</source>
        <translation>0 (всё устройство)</translation>
    </message>
    <message>
        <source>end of device</source>
        <translation>конец устройства</translation>
    </message>
    <message>
        <source>Start LBA:</source>
        <translation>Начальный LBA:</translation>
    </message>
    <message>
        <source>End LBA:</source>
        <translation>Конечный LBA:</translation>
    </message>
    <message>
        <source>Leave both LBAs blank to erase the WHOLE device. Overwriting cannot reach blocks the drive has remapped (flash wear levelling, reallocated sectors); the report says so. Hardware secure erase / sanitize is not offered.</source>
        <translation>Оставьте оба LBA пустыми, чтобы стереть ВСЁ устройство. Перезапись не достаёт блоков, которые накопитель переназначил (выравнивание износа флеш-памяти, переназначенные секторы); отчёт сообщает об этом. Аппаратное безопасное стирание / sanitize не предлагается.</translation>
    </message>
    <message>
        <source>The erase report appears here when the operation ends.</source>
        <translation>Отчёт о стирании появится здесь по окончании операции.</translation>
    </message>
    <message>
        <source>No device selected.</source>
        <translation>Устройство не выбрано.</translation>
    </message>
    <message>
        <source>end (%1)</source>
        <translation>конец (%1)</translation>
    </message>
    <message>
        <source>Ready. Nothing is written until you confirm.</source>
        <translation>Готово. Пока вы не подтвердите, ничего не записывается.</translation>
    </message>
    <message>
        <source>Enter the pattern as an even number of hex digits.</source>
        <translation>Введите шаблон в виде чётного числа hex-цифр.</translation>
    </message>
    <message>
        <source>Erase</source>
        <translation>Стирание</translation>
    </message>
    <message>
        <source>Enter an End LBA, or clear Start LBA to erase the whole device.</source>
        <translation>Укажите конечный LBA или очистите начальный LBA, чтобы стереть всё устройство.</translation>
    </message>
    <message>
        <source>End LBA must not be smaller than Start LBA.</source>
        <translation>Конечный LBA не может быть меньше начального.</translation>
    </message>
    <message numerus="yes">
        <source>%n pass(es) of random data</source>
        <translation>
            <numerusform>%n проход случайных данных</numerusform>
            <numerusform>%n прохода случайных данных</numerusform>
            <numerusform>%n проходов случайных данных</numerusform>
        </translation>
    </message>
    <message>
        <source>%1; verification: %2</source>
        <translation>%1; проверка: %2</translation>
    </message>
    <message>
        <source>Confirm Erase</source>
        <translation>Подтверждение стирания</translation>
    </message>
    <message>
        <source>Erase refused: %1</source>
        <translation>Стирание отклонено: %1</translation>
    </message>
    <message>
        <source>Invalid erase settings: %1</source>
        <translation>Недопустимые параметры стирания: %1</translation>
    </message>
    <message>
        <source>Erase started: %1</source>
        <translation>Стирание начато: %1</translation>
    </message>
    <message>
        <source>Stopping after the current request...</source>
        <translation>Остановка после текущего запроса...</translation>
    </message>
    <message>
        <source>Pass %1/%2  %3 %  %4 MB/s  errors %5</source>
        <translation>Проход %1/%2  %3 %  %4 МБ/с  ошибок %5</translation>
    </message>
    <message>
        <source>Erase finished: %1</source>
        <translation>Стирание завершено: %1</translation>
    </message>
    <message>
        <source>Could not save to history: %1</source>
        <translation>Не удалось сохранить в историю: %1</translation>
    </message>
    <message>
        <source>Save Erase Report</source>
        <translation>Сохранение отчёта о стирании</translation>
    </message>
    <message>
        <source>JSON report (*.json);;Text report (*.txt)</source>
        <translation>Отчёт JSON (*.json);;Текстовый отчёт (*.txt)</translation>
    </message>
    <message>
        <source>Cannot write %1:
%2</source>
        <translation>Не удаётся записать %1:
%2</translation>
    </message>
</context>
<context>
    <name>platter::gui::HelpDialog</name>
    <message>
        <source>platter Help</source>
        <translation>Справка platter</translation>
    </message>
    <message>
        <source>&amp;Close</source>
        <translation>&amp;Закрыть</translation>
    </message>
</context>
<context>
    <name>platter::gui::HexTab</name>
    <message>
        <source>LBA</source>
        <translation>LBA</translation>
    </message>
    <message>
        <source>Byte offset</source>
        <translation>Смещение в байтах</translation>
    </message>
    <message>
        <source>number or 0x…</source>
        <translation>число или 0x…</translation>
    </message>
    <message>
        <source>&amp;Go</source>
        <translation>&amp;Перейти</translation>
    </message>
    <message>
        <source>◀ &amp;Prev</source>
        <translation>◀ &amp;Назад</translation>
    </message>
    <message>
        <source>&amp;Next ▶</source>
        <translation>&amp;Далее ▶</translation>
    </message>
    <message>
        <source> sector(s)</source>
        <translation> сект.</translation>
    </message>
    <message>
        <source>Interpret: automatic</source>
        <translation>Разбор: автоматически</translation>
    </message>
    <message>
        <source>Interpret: none</source>
        <translation>Разбор: нет</translation>
    </message>
    <message>
        <source>Interpret as: %1</source>
        <translation>Разобрать как: %1</translation>
    </message>
    <message>
        <source>Offset</source>
        <translation>Смещение</translation>
    </message>
    <message>
        <source>Size</source>
        <translation>Размер</translation>
    </message>
    <message>
        <source>Field</source>
        <translation>Поле</translation>
    </message>
    <message>
        <source>Value</source>
        <translation>Значение</translation>
    </message>
    <message>
        <source>Note</source>
        <translation>Примечание</translation>
    </message>
    <message>
        <source>Structure</source>
        <translation>Структура</translation>
    </message>
    <message>
        <source>Matches</source>
        <translation>Совпадения</translation>
    </message>
    <message>
        <source>Hex bytes</source>
        <translation>Hex-байты</translation>
    </message>
    <message>
        <source>ASCII text</source>
        <translation>Текст ASCII</translation>
    </message>
    <message>
        <source>UTF-16 text</source>
        <translation>Текст UTF-16</translation>
    </message>
    <message>
        <source>e.g. 55 AA ?? 0F  (?? matches any byte)</source>
        <translation>например 55 AA ?? 0F  (?? — любой байт)</translation>
    </message>
    <message>
        <source>Ignore case</source>
        <translation>Не учитывать регистр</translation>
    </message>
    <message>
        <source>Find N&amp;ext</source>
        <translation>Найти &amp;далее</translation>
    </message>
    <message>
        <source>Find &amp;All</source>
        <translation>Найти &amp;все</translation>
    </message>
    <message>
        <source>&amp;Cancel</source>
        <translation>О&amp;тмена</translation>
    </message>
    <message>
        <source>text to look for</source>
        <translation>искомый текст</translation>
    </message>
    <message>
        <source>No device selected.</source>
        <translation>Устройство не выбрано.</translation>
    </message>
    <message>
        <source>Select a device or open an image to inspect its sectors (read-only).</source>
        <translation>Выберите устройство или откройте образ, чтобы просмотреть его секторы (только чтение).</translation>
    </message>
    <message>
        <source>Nothing has been read yet.

Press Go to read the sector above (empty = LBA 0). Reads are strictly read-only; physical disks usually need elevated rights.</source>
        <translation>Пока ничего не прочитано.

Нажмите «Перейти», чтобы прочитать указанный выше сектор (пусто = LBA 0). Чтение строго только на чтение; для физических дисков обычно нужны повышенные права.</translation>
    </message>
    <message>
        <source>%1 sectors of %2 bytes</source>
        <translation>секторов: %1 по %2 байт</translation>
    </message>
    <message>
        <source>Read-only inspector for %1.</source>
        <translation>Просмотр %1 (только чтение).</translation>
    </message>
    <message>
        <source>Enter a decimal number or 0x… hexadecimal.</source>
        <translation>Введите десятичное число или шестнадцатеричное вида 0x….</translation>
    </message>
    <message>
        <source>LBA %1 is beyond the end of the device (%2 sectors).</source>
        <translation>LBA %1 за концом устройства (секторов: %2).</translation>
    </message>
    <message>
        <source>LBA %1 is beyond the end of the device.</source>
        <translation>LBA %1 за концом устройства.</translation>
    </message>
    <message>
        <source>Cannot open the device for reading: %1</source>
        <translation>Не удаётся открыть устройство для чтения: %1</translation>
    </message>
    <message>
        <source>Read failed at LBA %1: %2</source>
        <translation>Сбой чтения на LBA %1: %2</translation>
    </message>
    <message>
        <source>LBA %1 of %2   byte offset %3 (0x%4)</source>
        <translation>LBA %1 из %2   смещение в байтах %3 (0x%4)</translation>
    </message>
    <message>
        <source>(nothing to show)</source>
        <translation>(нечего показывать)</translation>
    </message>
    <message>
        <source>Cannot interpret as %1: %2</source>
        <translation>Не удаётся разобрать как %1: %2</translation>
    </message>
    <message>
        <source>These bytes are not a structure platter recognises. Pick &quot;Interpret as&quot; to force one.</source>
        <translation>Эти байты не являются структурой, которую распознаёт platter. Выберите «Разобрать как», чтобы задать структуру вручную.</translation>
    </message>
    <message>
        <source>Type something to look for.</source>
        <translation>Введите, что искать.</translation>
    </message>
    <message>
        <source>Nothing left to search after this position.</source>
        <translation>После этой позиции искать больше нечего.</translation>
    </message>
    <message>
        <source>Searching…</source>
        <translation>Поиск…</translation>
    </message>
    <message>
        <source>Search failed: %1</source>
        <translation>Поиск не удался: %1</translation>
    </message>
    <message>
        <source> (%1 unreadable sectors skipped)</source>
        <translation> (пропущено нечитаемых секторов: %1)</translation>
    </message>
    <message>
        <source>LBA %1 + %2   (byte %3)</source>
        <translation>LBA %1 + %2   (байт %3)</translation>
    </message>
    <message>
        <source>No matches%1.</source>
        <translation>Совпадений нет%1.</translation>
    </message>
    <message>
        <source>%1 match(es)%2%3 — double-click one in the Matches tab to jump to it.</source>
        <translation>Совпадений: %1%2%3 — дважды щёлкните одно из них на вкладке «Совпадения», чтобы перейти к нему.</translation>
    </message>
    <message>
        <source> (stopped at the first %1)</source>
        <translation> (остановлено на первых %1)</translation>
    </message>
    <message>
        <source>Search found %1 match(es)</source>
        <translation>Поиск нашёл совпадений: %1</translation>
    </message>
    <message>
        <source>No further matches%1.</source>
        <translation>Больше совпадений нет%1.</translation>
    </message>
    <message>
        <source>Search cancelled.</source>
        <translation>Поиск отменён.</translation>
    </message>
    <message>
        <source>Match at LBA %1 + %2 (byte %3). Find Next continues after it.</source>
        <translation>Совпадение на LBA %1 + %2 (байт %3). «Найти далее» продолжит поиск после него.</translation>
    </message>
</context>
<context>
    <name>platter::gui::HistoryDialog</name>
    <message>
        <source>Operation History</source>
        <translation>История операций</translation>
    </message>
    <message>
        <source>All kinds</source>
        <translation>Все виды</translation>
    </message>
    <message>
        <source>All devices</source>
        <translation>Все устройства</translation>
    </message>
    <message>
        <source>Show:</source>
        <translation>Показать:</translation>
    </message>
    <message>
        <source>&amp;Refresh</source>
        <translation>&amp;Обновить</translation>
    </message>
    <message>
        <source>Id</source>
        <translation>Номер</translation>
    </message>
    <message>
        <source>Kind</source>
        <translation>Вид</translation>
    </message>
    <message>
        <source>Started (UTC)</source>
        <translation>Начало (UTC)</translation>
    </message>
    <message>
        <source>Device</source>
        <translation>Устройство</translation>
    </message>
    <message>
        <source>State</source>
        <translation>Состояние</translation>
    </message>
    <message>
        <source>Result</source>
        <translation>Результат</translation>
    </message>
    <message>
        <source>Select a saved session to see its report.</source>
        <translation>Выберите сохранённый сеанс, чтобы увидеть его отчёт.</translation>
    </message>
    <message>
        <source>&amp;Save Report...</source>
        <translation>&amp;Сохранить отчёт...</translation>
    </message>
    <message>
        <source>&amp;Delete Record...</source>
        <translation>&amp;Удалить запись...</translation>
    </message>
    <message>
        <source>&amp;Close</source>
        <translation>&amp;Закрыть</translation>
    </message>
    <message>
        <source>The history database is not available, so nothing was saved.</source>
        <translation>База истории недоступна, поэтому ничего не сохранено.</translation>
    </message>
    <message>
        <source>Cannot read the history: %1</source>
        <translation>Не удаётся прочитать историю: %1</translation>
    </message>
    <message>
        <source>Nothing saved yet for this filter. Results are saved when &quot;Record results in history&quot; is on.</source>
        <translation>Для этого фильтра пока ничего не сохранено. Результаты сохраняются, когда включён пункт «Вести историю».</translation>
    </message>
    <message>
        <source>Cannot show this record: %1</source>
        <translation>Не удаётся показать эту запись: %1</translation>
    </message>
    <message>
        <source>Save Report</source>
        <translation>Сохранение отчёта</translation>
    </message>
    <message>
        <source>JSON report (*.json);;Text report (*.txt)</source>
        <translation>Отчёт JSON (*.json);;Текстовый отчёт (*.txt)</translation>
    </message>
    <message>
        <source>Cannot write %1:
%2</source>
        <translation>Не удаётся записать %1:
%2</translation>
    </message>
    <message>
        <source>Delete Record</source>
        <translation>Удаление записи</translation>
    </message>
    <message>
        <source>Remove the saved %1 record #%2 (%3) from the history?

This only deletes the saved record. Nothing on any device is touched.</source>
        <translation>Удалить сохранённую запись «%1» №%2 (%3) из истории?

Удаляется только сохранённая запись. Ничего на устройствах не затрагивается.</translation>
    </message>
</context>
<context>
    <name>platter::gui::ImagingTab</name>
    <message>
        <source>Create image from the selected device</source>
        <translation>Создать образ выбранного устройства</translation>
    </message>
    <message>
        <source>Restore an image onto the selected device</source>
        <translation>Восстановить образ на выбранное устройство</translation>
    </message>
    <message>
        <source>Clone the selected device onto another device</source>
        <translation>Клонировать выбранное устройство на другое</translation>
    </message>
    <message>
        <source>Verify an image</source>
        <translation>Проверить образ</translation>
    </message>
    <message>
        <source>&amp;Start...</source>
        <translation>&amp;Старт...</translation>
    </message>
    <message>
        <source>S&amp;top</source>
        <translation>С&amp;топ</translation>
    </message>
    <message>
        <source>&amp;Save Report...</source>
        <translation>&amp;Сохранить отчёт...</translation>
    </message>
    <message>
        <source>Settings</source>
        <translation>Параметры</translation>
    </message>
    <message>
        <source>...</source>
        <translation>...</translation>
    </message>
    <message>
        <source>Image file:</source>
        <translation>Файл образа:</translation>
    </message>
    <message>
        <source>None (raw)</source>
        <translation>Нет (сырой)</translation>
    </message>
    <message>
        <source>zstd</source>
        <translation>zstd</translation>
    </message>
    <message>
        <source>Compression:</source>
        <translation>Сжатие:</translation>
    </message>
    <message>
        <source>zstd level:</source>
        <translation>Уровень zstd:</translation>
    </message>
    <message>
        <source>Raw: store empty blocks as holes</source>
        <translation>Сырой: хранить пустые блоки как «дыры»</translation>
    </message>
    <message>
        <source>Replace an existing file</source>
        <translation>Заменять существующий файл</translation>
    </message>
    <message>
        <source>Recovery mode (skip unreadable sectors)</source>
        <translation>Режим восстановления (пропускать нечитаемые секторы)</translation>
    </message>
    <message>
        <source>Unreadable sectors are replaced by zeros and listed in the report.</source>
        <translation>Нечитаемые секторы заменяются нулями и перечисляются в отчёте.</translation>
    </message>
    <message>
        <source>Retries:</source>
        <translation>Повторы:</translation>
    </message>
    <message>
        <source>Verify afterwards (read back and compare)</source>
        <translation>Проверить после (прочитать обратно и сравнить)</translation>
    </message>
    <message>
        <source>Also compare with the selected device</source>
        <translation>Также сравнить с выбранным устройством</translation>
    </message>
    <message>
        <source>Clone onto:</source>
        <translation>Клонировать на:</translation>
    </message>
    <message>
        <source>0</source>
        <translation>0</translation>
    </message>
    <message>
        <source>end of device</source>
        <translation>конец устройства</translation>
    </message>
    <message>
        <source>Start LBA:</source>
        <translation>Начальный LBA:</translation>
    </message>
    <message>
        <source>End LBA:</source>
        <translation>Конечный LBA:</translation>
    </message>
    <message>
        <source>Target LBA:</source>
        <translation>LBA назначения:</translation>
    </message>
    <message>
        <source>optional SHA-256 (64 hex digits)</source>
        <translation>необязательный SHA-256 (64 hex-цифры)</translation>
    </message>
    <message>
        <source>Expected SHA-256:</source>
        <translation>Ожидаемый SHA-256:</translation>
    </message>
    <message>
        <source>Reading a failing drive stresses it. A recovery copy replaces unreadable sectors with zeros; it is not a substitute for a specialised recovery tool.</source>
        <translation>Чтение неисправного накопителя нагружает его. Копия в режиме восстановления заменяет нечитаемые секторы нулями; она не заменяет специализированные средства восстановления данных.</translation>
    </message>
    <message>
        <source>The report appears here when the operation ends.</source>
        <translation>Отчёт появится здесь по окончании операции.</translation>
    </message>
    <message>
        <source>No device selected.</source>
        <translation>Устройство не выбрано.</translation>
    </message>
    <message>
        <source>end (%1)</source>
        <translation>конец (%1)</translation>
    </message>
    <message>
        <source>Ready. Restore and clone write nothing until you confirm.</source>
        <translation>Готово. Восстановление и клонирование ничего не записывают, пока вы не подтвердите.</translation>
    </message>
    <message>
        <source>Where the image starts on the comparison device</source>
        <translation>С какого места на сравниваемом устройстве начинается образ</translation>
    </message>
    <message>
        <source>First source LBA</source>
        <translation>Первый LBA источника</translation>
    </message>
    <message>
        <source>&amp;Create...</source>
        <translation>&amp;Создать...</translation>
    </message>
    <message>
        <source>&amp;Restore...</source>
        <translation>&amp;Восстановить...</translation>
    </message>
    <message>
        <source>&amp;Clone...</source>
        <translation>&amp;Клонировать...</translation>
    </message>
    <message>
        <source>&amp;Verify</source>
        <translation>&amp;Проверить</translation>
    </message>
    <message>
        <source>Disk images (*.img *.img.zst *.zst *.raw *.bin);;All files (*)</source>
        <translation>Образы дисков (*.img *.img.zst *.zst *.raw *.bin);;Все файлы (*)</translation>
    </message>
    <message>
        <source>Image file</source>
        <translation>Файл образа</translation>
    </message>
    <message>
        <source>Clone / Image</source>
        <translation>Клон / образ</translation>
    </message>
    <message>
        <source>%1 is not a valid LBA.</source>
        <translation>%1 не является допустимым LBA.</translation>
    </message>
    <message>
        <source>%1 could not start: %2</source>
        <translation>%1: не удалось начать: %2</translation>
    </message>
    <message>
        <source>%1 started</source>
        <translation>%1: начато</translation>
    </message>
    <message>
        <source>Choose the image file to create.</source>
        <translation>Выберите файл образа для создания.</translation>
    </message>
    <message>
        <source>Start LBA</source>
        <translation>Начальный LBA</translation>
    </message>
    <message>
        <source>End LBA</source>
        <translation>Конечный LBA</translation>
    </message>
    <message>
        <source>Cannot open the device for reading: %1</source>
        <translation>Не удаётся открыть устройство для чтения: %1</translation>
    </message>
    <message>
        <source>Image creation</source>
        <translation>Создание образа</translation>
    </message>
    <message>
        <source>Choose the image file to restore.</source>
        <translation>Выберите файл образа для восстановления.</translation>
    </message>
    <message>
        <source>Target LBA</source>
        <translation>LBA назначения</translation>
    </message>
    <message>
        <source>Restore image %1 (%2)</source>
        <translation>Восстановление образа %1 (%2)</translation>
    </message>
    <message>
        <source>Confirm Restore</source>
        <translation>Подтверждение восстановления</translation>
    </message>
    <message>
        <source>Restore refused: %1</source>
        <translation>Восстановление отклонено: %1</translation>
    </message>
    <message>
        <source>Image restore</source>
        <translation>Восстановление образа</translation>
    </message>
    <message>
        <source>Choose the device to clone onto.</source>
        <translation>Выберите устройство, на которое клонировать.</translation>
    </message>
    <message>
        <source>The source and the target are the same device.</source>
        <translation>Источник и цель — одно и то же устройство.</translation>
    </message>
    <message>
        <source>The source range is outside the device.</source>
        <translation>Диапазон источника выходит за пределы устройства.</translation>
    </message>
    <message>
        <source>The devices have different sector sizes (%1 and %2); a sector-for-sector clone is not possible.</source>
        <translation>У устройств разные размеры секторов (%1 и %2); посекторное клонирование невозможно.</translation>
    </message>
    <message>
        <source>Cannot open the source for reading: %1</source>
        <translation>Не удаётся открыть источник для чтения: %1</translation>
    </message>
    <message>
        <source>Clone %1 (%2) onto this device</source>
        <translation>Клонирование %1 (%2) на это устройство</translation>
    </message>
    <message>
        <source>Confirm Clone</source>
        <translation>Подтверждение клонирования</translation>
    </message>
    <message>
        <source>Clone refused: %1</source>
        <translation>Клонирование отклонено: %1</translation>
    </message>
    <message>
        <source>Clone</source>
        <translation>Клонирование</translation>
    </message>
    <message>
        <source>Choose the image file to verify.</source>
        <translation>Выберите файл образа для проверки.</translation>
    </message>
    <message>
        <source>Image verification</source>
        <translation>Проверка образа</translation>
    </message>
    <message>
        <source>Stopping after the current block...</source>
        <translation>Остановка после текущего блока...</translation>
    </message>
    <message>
        <source>%1 %  %2 MB/s  errors %3  %4</source>
        <translation>%1 %  %2 МБ/с  ошибок %3  %4</translation>
    </message>
    <message>
        <source>%1 finished: %2</source>
        <translation>%1 завершено: %2</translation>
    </message>
    <message>
        <source>Could not save to history: %1</source>
        <translation>Не удалось сохранить в историю: %1</translation>
    </message>
    <message>
        <source>Save Report</source>
        <translation>Сохранение отчёта</translation>
    </message>
    <message>
        <source>JSON report (*.json);;Text report (*.txt)</source>
        <translation>Отчёт JSON (*.json);;Текстовый отчёт (*.txt)</translation>
    </message>
    <message>
        <source>Cannot write %1:
%2</source>
        <translation>Не удаётся записать %1:
%2</translation>
    </message>
</context>
<context>
    <name>platter::gui::InfoTab</name>
    <message>
        <source>Identification</source>
        <translation>Идентификация</translation>
    </message>
    <message>
        <source>Property</source>
        <translation>Свойство</translation>
    </message>
    <message>
        <source>Value</source>
        <translation>Значение</translation>
    </message>
    <message>
        <source>Capabilities</source>
        <translation>Возможности</translation>
    </message>
    <message>
        <source>Capability</source>
        <translation>Возможность</translation>
    </message>
    <message>
        <source>Supported</source>
        <translation>Поддерживается</translation>
    </message>
    <message>
        <source>Source</source>
        <translation>Источник</translation>
    </message>
    <message>
        <source>Device path</source>
        <translation>Путь к устройству</translation>
    </message>
    <message>
        <source>Vendor</source>
        <translation>Производитель</translation>
    </message>
    <message>
        <source>Model</source>
        <translation>Модель</translation>
    </message>
    <message>
        <source>Serial number</source>
        <translation>Серийный номер</translation>
    </message>
    <message>
        <source>Firmware revision</source>
        <translation>Версия прошивки</translation>
    </message>
    <message>
        <source>Stable identifier</source>
        <translation>Устойчивый идентификатор</translation>
    </message>
    <message>
        <source>Capacity</source>
        <translation>Ёмкость</translation>
    </message>
    <message>
        <source>Total LBAs</source>
        <translation>Всего LBA</translation>
    </message>
    <message>
        <source>Logical sector size</source>
        <translation>Размер логического сектора</translation>
    </message>
    <message>
        <source> bytes</source>
        <translation> байт</translation>
    </message>
    <message>
        <source>Physical sector size</source>
        <translation>Размер физического сектора</translation>
    </message>
    <message>
        <source>Interface</source>
        <translation>Интерфейс</translation>
    </message>
    <message>
        <source>Media type</source>
        <translation>Тип носителя</translation>
    </message>
    <message>
        <source>Removable</source>
        <translation>Съёмное</translation>
    </message>
    <message>
        <source>Read-only</source>
        <translation>Только чтение</translation>
    </message>
    <message>
        <source>TRIM / discard</source>
        <translation>TRIM / discard</translation>
    </message>
    <message>
        <source>Mounted at</source>
        <translation>Смонтировано в</translation>
    </message>
    <message>
        <source>Held by</source>
        <translation>Занято</translation>
    </message>
    <message>
        <source>System disk</source>
        <translation>Системный диск</translation>
    </message>
    <message>
        <source>YES</source>
        <translation>ДА</translation>
    </message>
    <message>
        <source>No</source>
        <translation>Нет</translation>
    </message>
    <message>
        <source>Partition table</source>
        <translation>Таблица разделов</translation>
    </message>
    <message>
        <source>SMART protocol</source>
        <translation>Протокол SMART</translation>
    </message>
    <message>
        <source>Rotation rate</source>
        <translation>Скорость вращения</translation>
    </message>
    <message>
        <source> rpm</source>
        <translation> об/мин</translation>
    </message>
    <message>
        <source>Solid state</source>
        <translation>Твердотельный</translation>
    </message>
    <message>
        <source>Bus topology</source>
        <translation>Топология шины</translation>
    </message>
    <message>
        <source>Yes</source>
        <translation>Да</translation>
    </message>
    <message>
        <source>image file</source>
        <translation>файл образа</translation>
    </message>
    <message>
        <source>OS device query</source>
        <translation>запрос к ОС</translation>
    </message>
    <message>
        <source>Not probed</source>
        <translation>Не опрашивалось</translation>
    </message>
    <message>
        <source>run SMART query</source>
        <translation>выполните запрос SMART</translation>
    </message>
    <message>
        <source>smartctl</source>
        <translation>smartctl</translation>
    </message>
    <message>
        <source>not probed (v0.4+)</source>
        <translation>не опрашивалось (v0.4+)</translation>
    </message>
</context>
<context>
    <name>platter::gui::LogModel</name>
    <message>
        <source>DEBUG</source>
        <translation>ОТЛАДКА</translation>
    </message>
    <message>
        <source>INFO</source>
        <translation>ИНФО</translation>
    </message>
    <message>
        <source>WARNING</source>
        <translation>ПРЕДУПРЕЖДЕНИЕ</translation>
    </message>
    <message>
        <source>ERROR</source>
        <translation>ОШИБКА</translation>
    </message>
    <message>
        <source>CRITICAL</source>
        <translation>КРИТИЧНО</translation>
    </message>
    <message>
        <source>Time</source>
        <translation>Время</translation>
    </message>
    <message>
        <source>Severity</source>
        <translation>Важность</translation>
    </message>
    <message>
        <source>Device</source>
        <translation>Устройство</translation>
    </message>
    <message>
        <source>Description</source>
        <translation>Описание</translation>
    </message>
</context>
<context>
    <name>platter::gui::MainWindow</name>
    <message>
        <source>platter %1 started (%2 privileges; destructive operations ask for typed confirmation)</source>
        <translation>platter %1 запущен (права: %2; деструктивные операции требуют подтверждения вводом текста)</translation>
    </message>
    <message>
        <source>Planned for %1</source>
        <translation>Запланировано на %1</translation>
    </message>
    <message>
        <source>&amp;Open Disk Image...</source>
        <translation>&amp;Открыть образ диска...</translation>
    </message>
    <message>
        <source>Open a raw image file read-only</source>
        <translation>Открыть файл «сырого» образа только для чтения</translation>
    </message>
    <message>
        <source>&amp;Save Report...</source>
        <translation>&amp;Сохранить отчёт...</translation>
    </message>
    <message>
        <source>Save a JSON report for the selected device</source>
        <translation>Сохранить JSON-отчёт по выбранному устройству</translation>
    </message>
    <message>
        <source>E&amp;xit</source>
        <translation>&amp;Выход</translation>
    </message>
    <message>
        <source>&amp;Refresh Device List</source>
        <translation>&amp;Обновить список устройств</translation>
    </message>
    <message>
        <source>Re-enumerate storage devices (metadata only)</source>
        <translation>Заново перечислить устройства хранения (только метаданные)</translation>
    </message>
    <message>
        <source>&amp;Identify Device</source>
        <translation>&amp;Опознать устройство</translation>
    </message>
    <message>
        <source>&amp;SMART Information</source>
        <translation>&amp;Сведения SMART</translation>
    </message>
    <message>
        <source>Query SMART / NVMe health via smartctl (read-only)</source>
        <translation>Запросить состояние SMART / NVMe через smartctl (только чтение)</translation>
    </message>
    <message>
        <source>Device &amp;Capabilities</source>
        <translation>&amp;Возможности устройства</translation>
    </message>
    <message>
        <source>Read &amp;Partition Table</source>
        <translation>Прочитать &amp;таблицу разделов</translation>
    </message>
    <message>
        <source>&amp;Surface Test...</source>
        <translation>Проверка &amp;поверхности...</translation>
    </message>
    <message>
        <source>Read-only surface scan with a latency map</source>
        <translation>Сканирование поверхности только чтением с картой задержек</translation>
    </message>
    <message>
        <source>&amp;Benchmark...</source>
        <translation>&amp;Тест производительности...</translation>
    </message>
    <message>
        <source>Read benchmarks: sequential, random, access time, speed curve, sweeps</source>
        <translation>Тесты чтения: последовательное, случайное, время доступа, кривая скорости, перебор параметров</translation>
    </message>
    <message>
        <source>&amp;Record History</source>
        <translation>Вести &amp;историю</translation>
    </message>
    <message>
        <source>Save finished benchmarks, scans and SMART queries to the local history database</source>
        <translation>Сохранять завершённые тесты, сканирования и запросы SMART в локальную базу истории</translation>
    </message>
    <message>
        <source>Show Serial &amp;Numbers</source>
        <translation>Показывать &amp;серийные номера</translation>
    </message>
    <message>
        <source>Serial numbers are masked on screen and omitted from reports by default</source>
        <translation>По умолчанию серийные номера скрыты на экране и не попадают в отчёты</translation>
    </message>
    <message>
        <source>&amp;About platter...</source>
        <translation>&amp;О программе platter...</translation>
    </message>
    <message>
        <source>&amp;License Information...</source>
        <translation>&amp;Лицензия...</translation>
    </message>
    <message>
        <source>&amp;File</source>
        <translation>&amp;Файл</translation>
    </message>
    <message>
        <source>Use &amp;Privileged Helper</source>
        <translation>Использовать &amp;привилегированный помощник</translation>
    </message>
    <message>
        <source>Not needed: platter is already running with full privileges</source>
        <translation>Не нужно: platter уже запущен с полными правами</translation>
    </message>
    <message>
        <source>Open disks through a small root helper (asks for your password once) instead of running the whole program as root</source>
        <translation>Открывать диски через небольшой помощник с правами root (пароль запрашивается один раз), а не запускать всю программу от root</translation>
    </message>
    <message>
        <source>&amp;Device</source>
        <translation>&amp;Устройство</translation>
    </message>
    <message>
        <source>&amp;Operations</source>
        <translation>&amp;Операции</translation>
    </message>
    <message>
        <source>&amp;Partition Manager...</source>
        <translation>&amp;Менеджер разделов...</translation>
    </message>
    <message>
        <source>Edit the partition table and format partitions (asks for typed confirmation)</source>
        <translation>Правка таблицы разделов и форматирование разделов (требует подтверждения вводом текста)</translation>
    </message>
    <message>
        <source>&amp;Erase Device...</source>
        <translation>&amp;Стереть устройство...</translation>
    </message>
    <message>
        <source>Overwrite erase with verification (asks for typed confirmation)</source>
        <translation>Стирание перезаписью с проверкой (требует подтверждения вводом текста)</translation>
    </message>
    <message>
        <source>&amp;Clone Device...</source>
        <translation>&amp;Клонировать устройство...</translation>
    </message>
    <message>
        <source>Copy the selected device onto another (asks for typed confirmation)</source>
        <translation>Скопировать выбранное устройство на другое (требует подтверждения вводом текста)</translation>
    </message>
    <message>
        <source>Create Disk &amp;Image...</source>
        <translation>Создать &amp;образ диска...</translation>
    </message>
    <message>
        <source>Read the selected device into an image file</source>
        <translation>Прочитать выбранное устройство в файл образа</translation>
    </message>
    <message>
        <source>&amp;Qualify Drive...</source>
        <translation>&amp;Квалификация накопителя...</translation>
    </message>
    <message>
        <source>Burn-in: write passes, read-back, read pass, random reads (asks for typed confirmation)</source>
        <translation>Прогон под нагрузкой: проходы записи, контрольное чтение, проход чтения, случайные чтения (требует подтверждения вводом текста)</translation>
    </message>
    <message>
        <source>&amp;Tools</source>
        <translation>С&amp;ервис</translation>
    </message>
    <message>
        <source>&amp;Hex Sector Viewer</source>
        <translation>&amp;Hex-просмотр секторов</translation>
    </message>
    <message>
        <source>Inspect sectors read-only, with structure interpretation and search</source>
        <translation>Просмотр секторов только для чтения с разбором структур и поиском</translation>
    </message>
    <message>
        <source>&amp;Checksum Calculator...</source>
        <translation>&amp;Контрольная сумма...</translation>
    </message>
    <message>
        <source>SHA-256 of any file</source>
        <translation>SHA-256 любого файла</translation>
    </message>
    <message>
        <source>Image &amp;Verification</source>
        <translation>&amp;Проверка образа</translation>
    </message>
    <message>
        <source>Check an image against its checksum or against a device</source>
        <translation>Сверить образ с контрольной суммой или с устройством</translation>
    </message>
    <message>
        <source>Operation &amp;History...</source>
        <translation>&amp;История операций...</translation>
    </message>
    <message>
        <source>Browse every saved benchmark, scan, erase, imaging and qualification report</source>
        <translation>Просмотр всех сохранённых отчётов о тестах, сканированиях, стираниях, образах и квалификации</translation>
    </message>
    <message>
        <source>&amp;Settings</source>
        <translation>&amp;Настройки</translation>
    </message>
    <message>
        <source>&amp;Preferences...</source>
        <translation>&amp;Параметры...</translation>
    </message>
    <message>
        <source>History, look, privileged helper, log file</source>
        <translation>История, внешний вид, привилегированный помощник, файл журнала</translation>
    </message>
    <message>
        <source>&amp;Help</source>
        <translation>&amp;Справка</translation>
    </message>
    <message>
        <source>&amp;Documentation</source>
        <translation>&amp;Документация</translation>
    </message>
    <message>
        <source>Built-in help (works offline)</source>
        <translation>Встроенная справка (работает без сети)</translation>
    </message>
    <message>
        <source>Main</source>
        <translation>Главная</translation>
    </message>
    <message>
        <source> Device: </source>
        <translation> Устройство: </translation>
    </message>
    <message>
        <source>Select a device or image (Ctrl+D)</source>
        <translation>Выберите устройство или образ (Ctrl+D)</translation>
    </message>
    <message>
        <source>Device Summary</source>
        <translation>Сведения об устройстве</translation>
    </message>
    <message>
        <source>Model:</source>
        <translation>Модель:</translation>
    </message>
    <message>
        <source>Capacity:</source>
        <translation>Ёмкость:</translation>
    </message>
    <message>
        <source>Temperature:</source>
        <translation>Температура:</translation>
    </message>
    <message>
        <source>Interface:</source>
        <translation>Интерфейс:</translation>
    </message>
    <message>
        <source>Sectors:</source>
        <translation>Секторы:</translation>
    </message>
    <message>
        <source>Health:</source>
        <translation>Состояние:</translation>
    </message>
    <message>
        <source>Firmware:</source>
        <translation>Прошивка:</translation>
    </message>
    <message>
        <source>Usage:</source>
        <translation>Использование:</translation>
    </message>
    <message>
        <source>INFO</source>
        <translation>СВЕДЕНИЯ</translation>
    </message>
    <message>
        <source>SMART</source>
        <translation>SMART</translation>
    </message>
    <message>
        <source>SURFACE</source>
        <translation>ПОВЕРХНОСТЬ</translation>
    </message>
    <message>
        <source>BENCHMARK</source>
        <translation>ТЕСТ СКОРОСТИ</translation>
    </message>
    <message>
        <source>PARTITIONS</source>
        <translation>РАЗДЕЛЫ</translation>
    </message>
    <message>
        <source>ERASE</source>
        <translation>СТИРАНИЕ</translation>
    </message>
    <message>
        <source>QUALIFY</source>
        <translation>КВАЛИФИКАЦИЯ</translation>
    </message>
    <message>
        <source>CLONE / IMAGE</source>
        <translation>КЛОН / ОБРАЗ</translation>
    </message>
    <message>
        <source>HEX</source>
        <translation>HEX</translation>
    </message>
    <message>
        <source>LOG</source>
        <translation>ЖУРНАЛ</translation>
    </message>
    <message>
        <source>Activity Log</source>
        <translation>Журнал событий</translation>
    </message>
    <message>
        <source>No device</source>
        <translation>Нет устройства</translation>
    </message>
    <message>
        <source>CONFIRM TO WRITE</source>
        <translation>ЗАПИСЬ ТОЛЬКО С ПОДТВЕРЖДЕНИЕМ</translation>
    </message>
    <message>
        <source>Nothing is written to a device without a preview and a typed confirmation</source>
        <translation>На устройство ничего не пишется без предпросмотра и подтверждения вводом текста</translation>
    </message>
    <message>
        <source>Privileges: %1</source>
        <translation>Права: %1</translation>
    </message>
    <message>
        <source>Running elevated</source>
        <translation>Запущено с повышенными правами</translation>
    </message>
    <message>
        <source>Reading sectors and SMART data from physical disks usually needs root / Administrator</source>
        <translation>Для чтения секторов и данных SMART физических дисков обычно нужны права root / администратора</translation>
    </message>
    <message>
        <source>History database unavailable (%1); results will not be saved</source>
        <translation>База истории недоступна (%1); результаты сохраняться не будут</translation>
    </message>
    <message>
        <source>History database: %1</source>
        <translation>База истории: %1</translation>
    </message>
    <message>
        <source>Operation Running</source>
        <translation>Операция выполняется</translation>
    </message>
    <message>
        <source>A scan, benchmark or erase is still running. Stop it and exit?</source>
        <translation>Ещё выполняется сканирование, тест или стирание. Остановить его и выйти?</translation>
    </message>
    <message>
        <source>Device enumeration failed: %1</source>
        <translation>Не удалось перечислить устройства: %1</translation>
    </message>
    <message numerus="yes">
        <source>Found %n storage device(s)</source>
        <translation>
            <numerusform>Найдено устройство хранения: %n</numerusform>
            <numerusform>Найдено устройства хранения: %n</numerusform>
            <numerusform>Найдено устройств хранения: %n</numerusform>
        </translation>
    </message>
    <message>
        <source>Previously selected device %1 is no longer present</source>
        <translation>Ранее выбранное устройство %1 больше не обнаружено</translation>
    </message>
    <message>
        <source>Cannot open image: %1</source>
        <translation>Не удаётся открыть образ: %1</translation>
    </message>
    <message>
        <source>Open Disk Image</source>
        <translation>Открыть образ диска</translation>
    </message>
    <message>
        <source>Opened image read-only (%1-byte sectors)</source>
        <translation>Образ открыт только для чтения (секторы по %1 байт)</translation>
    </message>
    <message>
        <source>Disk images (*.img *.raw *.bin *.dd);;All files (*)</source>
        <translation>Образы дисков (*.img *.raw *.bin *.dd);;Все файлы (*)</translation>
    </message>
    <message>
        <source>UNKNOWN</source>
        <translation>НЕИЗВЕСТНО</translation>
    </message>
    <message>
        <source>%1 x %2 B (phys %3 B)</source>
        <translation>%1 × %2 Б (физ. %3 Б)</translation>
    </message>
    <message>
        <source>SYSTEM DISK  </source>
        <translation>СИСТЕМНЫЙ ДИСК  </translation>
    </message>
    <message>
        <source>Mounted: %1</source>
        <translation>Смонтировано: %1</translation>
    </message>
    <message>
        <source>In use by %1</source>
        <translation>Используется: %1</translation>
    </message>
    <message>
        <source>Not mounted</source>
        <translation>Не смонтировано</translation>
    </message>
    <message>
        <source>%1 °C</source>
        <translation>%1 °C</translation>
    </message>
    <message>
        <source>N/A</source>
        <translation>Н/Д</translation>
    </message>
    <message>
        <source>Querying SMART via smartctl</source>
        <translation>Запрос SMART через smartctl</translation>
    </message>
    <message>
        <source>SMART result discarded: device disappeared</source>
        <translation>Результат SMART отброшен: устройство пропало</translation>
    </message>
    <message>
        <source>SMART query failed: %1</source>
        <translation>Запрос SMART не удался: %1</translation>
    </message>
    <message>
        <source>SMART health: %1</source>
        <translation>Состояние SMART: %1</translation>
    </message>
    <message>
        <source>SMART snapshot not saved: %1</source>
        <translation>Снимок SMART не сохранён: %1</translation>
    </message>
    <message>
        <source>Cannot open device: %1</source>
        <translation>Не удаётся открыть устройство: %1</translation>
    </message>
    <message>
        <source>Partition table read failed: %1</source>
        <translation>Не удалось прочитать таблицу разделов: %1</translation>
    </message>
    <message>
        <source>Partition table: %1, %2 partition(s), %3 warning(s)</source>
        <translation>Таблица разделов: %1, разделов: %2, предупреждений: %3</translation>
    </message>
    <message>
        <source>Partition table: %1, %2 partition(s)</source>
        <translation>Таблица разделов: %1, разделов: %2</translation>
    </message>
    <message>
        <source>Logging to %1</source>
        <translation>Журнал пишется в %1</translation>
    </message>
    <message>
        <source>Cannot open the log file %1: %2</source>
        <translation>Не удаётся открыть файл журнала %1: %2</translation>
    </message>
    <message>
        <source>Language</source>
        <translation>Язык</translation>
    </message>
    <message>
        <source>The new language is used the next time platter starts.</source>
        <translation>Новый язык будет применён при следующем запуске platter.</translation>
    </message>
    <message>
        <source>The history database location changes after restarting platter</source>
        <translation>Расположение базы истории изменится после перезапуска platter</translation>
    </message>
    <message>
        <source>Privileged Helper</source>
        <translation>Привилегированный помощник</translation>
    </message>
    <message>
        <source>Privileged helper not enabled</source>
        <translation>Привилегированный помощник не включён</translation>
    </message>
    <message>
        <source>Privileges: user + helper</source>
        <translation>Права: пользователь + помощник</translation>
    </message>
    <message>
        <source>Privileged helper enabled</source>
        <translation>Привилегированный помощник включён</translation>
    </message>
    <message>
        <source>Privileged helper off</source>
        <translation>Привилегированный помощник выключен</translation>
    </message>
    <message>
        <source>Partition Manager</source>
        <translation>Менеджер разделов</translation>
    </message>
    <message>
        <source>Wait for the running operation to finish first.</source>
        <translation>Сначала дождитесь завершения текущей операции.</translation>
    </message>
    <message>
        <source>Save Report</source>
        <translation>Сохранить отчёт</translation>
    </message>
    <message>
        <source>Include serial numbers in the report?

They are omitted by default because reports are often shared.</source>
        <translation>Включить серийные номера в отчёт?

По умолчанию они не включаются, так как отчётами часто делятся.</translation>
    </message>
    <message>
        <source>JSON report (*.json);;All files (*)</source>
        <translation>Отчёт JSON (*.json);;Все файлы (*)</translation>
    </message>
    <message>
        <source>Cannot write %1:
%2</source>
        <translation>Не удаётся записать %1:
%2</translation>
    </message>
    <message>
        <source>Report saved to %1</source>
        <translation>Отчёт сохранён: %1</translation>
    </message>
    <message>
        <source>About platter</source>
        <translation>О программе platter</translation>
    </message>
    <message>
        <source>&lt;b&gt;platter %1&lt;/b&gt;&lt;br&gt;Storage diagnostics, maintenance and management utility.&lt;br&gt;&lt;br&gt;It identifies devices, reads SMART data via smartmontools, scans surfaces, runs benchmarks and inspects sectors without writing anything. Erasing, qualifying, partitioning, formatting, restoring and cloning WRITE to a device, and each one shows a preview and asks for a typed confirmation first.&lt;br&gt;&lt;br&gt;No telemetry. No network access. No registration.</source>
        <translation>&lt;b&gt;platter %1&lt;/b&gt;&lt;br&gt;Утилита для диагностики, обслуживания и управления накопителями.&lt;br&gt;&lt;br&gt;Она опознаёт устройства, читает данные SMART через smartmontools, сканирует поверхность, выполняет тесты производительности и изучает секторы, ничего не записывая. Стирание, квалификация, разметка, форматирование, восстановление и клонирование ЗАПИСЫВАЮТ данные на устройство, и каждая такая операция сначала показывает предпросмотр и требует подтверждения вводом текста.&lt;br&gt;&lt;br&gt;Никакой телеметрии. Никакого доступа к сети. Никакой регистрации.</translation>
    </message>
    <message>
        <source>License Information</source>
        <translation>Сведения о лицензии</translation>
    </message>
    <message>
        <source>platter: license to be decided by the author.

Third-party components:
  Qt 6 (LGPL v3)
  nlohmann/json (MIT)
  smartmontools (GPL v2, invoked as an external program)
</source>
        <translation>platter: лицензия будет определена автором.

Сторонние компоненты:
  Qt 6 (LGPL v3)
  nlohmann/json (MIT)
  smartmontools (GPL v2, запускается как внешняя программа)
</translation>
    </message>
</context>
<context>
    <name>platter::gui::PartitionBar</name>
    <message>
        <source>Unallocated
%1</source>
        <translation>Не размечено
%1</translation>
    </message>
    <message>
        <source>Partition table not read</source>
        <translation>Таблица разделов не прочитана</translation>
    </message>
    <message>
        <source>Unpartitioned</source>
        <translation>Без разделов</translation>
    </message>
</context>
<context>
    <name>platter::gui::PartitionEditorDialog</name>
    <message>
        <source>Partition Manager - %1</source>
        <translation>Менеджер разделов — %1</translation>
    </message>
    <message>
        <source>&amp;New Table...</source>
        <translation>&amp;Новая таблица...</translation>
    </message>
    <message>
        <source>&amp;Add...</source>
        <translation>&amp;Добавить...</translation>
    </message>
    <message>
        <source>&amp;Delete</source>
        <translation>&amp;Удалить</translation>
    </message>
    <message>
        <source>&amp;Properties...</source>
        <translation>&amp;Свойства...</translation>
    </message>
    <message>
        <source>&amp;Resize...</source>
        <translation>&amp;Изменить размер...</translation>
    </message>
    <message>
        <source>&amp;Format...</source>
        <translation>&amp;Форматировать...</translation>
    </message>
    <message>
        <source>#</source>
        <translation>№</translation>
    </message>
    <message>
        <source>Start LBA</source>
        <translation>Начальный LBA</translation>
    </message>
    <message>
        <source>End LBA</source>
        <translation>Конечный LBA</translation>
    </message>
    <message>
        <source>Size</source>
        <translation>Размер</translation>
    </message>
    <message>
        <source>Type</source>
        <translation>Тип</translation>
    </message>
    <message>
        <source>Name / Flags</source>
        <translation>Имя / флаги</translation>
    </message>
    <message>
        <source>Aligned</source>
        <translation>Выравнивание</translation>
    </message>
    <message>
        <source>Changes are staged here and written only when you press Apply and type the confirmation. Resizing or moving a partition changes the table only - file systems and data are not moved.</source>
        <translation>Изменения накапливаются здесь и записываются, только когда вы нажмёте «Применить» и введёте подтверждение. Изменение размера или перенос раздела меняет только таблицу — файловые системы и данные не перемещаются.</translation>
    </message>
    <message>
        <source>A&amp;pply Changes...</source>
        <translation>&amp;Применить изменения...</translation>
    </message>
    <message>
        <source>Re&amp;vert</source>
        <translation>От&amp;катить</translation>
    </message>
    <message>
        <source>&amp;Close</source>
        <translation>&amp;Закрыть</translation>
    </message>
    <message>
        <source>Cannot open the device: %1</source>
        <translation>Не удаётся открыть устройство: %1</translation>
    </message>
    <message>
        <source>Cannot read the partition table: %1</source>
        <translation>Не удаётся прочитать таблицу разделов: %1</translation>
    </message>
    <message>
        <source>This table cannot be edited: %1</source>
        <translation>Эту таблицу нельзя редактировать: %1</translation>
    </message>
    <message>
        <source>The device has no partition table. Use New Table to create one.</source>
        <translation>На устройстве нет таблицы разделов. Создайте её кнопкой «Новая таблица».</translation>
    </message>
    <message>
        <source>%1: %2 table, %3 partition(s)%4</source>
        <translation>%1: таблица %2, разделов: %3%4</translation>
    </message>
    <message>
        <source>   *** UNSAVED CHANGES ***</source>
        <translation>   *** ЕСТЬ НЕСОХРАНЁННЫЕ ИЗМЕНЕНИЯ ***</translation>
    </message>
    <message>
        <source>boot</source>
        <translation>загрузочный</translation>
    </message>
    <message>
        <source>extended</source>
        <translation>расширенный</translation>
    </message>
    <message>
        <source>logical</source>
        <translation>логический</translation>
    </message>
    <message>
        <source>1 MiB</source>
        <translation>1 МиБ</translation>
    </message>
    <message>
        <source>NO</source>
        <translation>НЕТ</translation>
    </message>
    <message>
        <source>ERROR: %1</source>
        <translation>ОШИБКА: %1</translation>
    </message>
    <message>
        <source>No problems found.</source>
        <translation>Проблем не найдено.</translation>
    </message>
    <message>
        <source>No staged changes.</source>
        <translation>Нет накопленных изменений.</translation>
    </message>
    <message>
        <source>New Partition Table</source>
        <translation>Новая таблица разделов</translation>
    </message>
    <message>
        <source>Scheme:</source>
        <translation>Схема:</translation>
    </message>
    <message>
        <source>The new table is empty. Applying it makes every existing partition unreachable.</source>
        <translation>Новая таблица пуста. Её применение сделает недоступными все существующие разделы.</translation>
    </message>
    <message>
        <source>New Table</source>
        <translation>Новая таблица</translation>
    </message>
    <message>
        <source>Add Partition</source>
        <translation>Добавление раздела</translation>
    </message>
    <message>
        <source>Type:</source>
        <translation>Тип:</translation>
    </message>
    <message>
        <source>e.g. 512M, 20G, 1.5T or max (fill the free space)</source>
        <translation>например 512M, 20G, 1.5T или max (всё свободное место)</translation>
    </message>
    <message>
        <source>Size:</source>
        <translation>Размер:</translation>
    </message>
    <message>
        <source>Name:</source>
        <translation>Имя:</translation>
    </message>
    <message>
        <source>Bootable (active)</source>
        <translation>Загрузочный (активный)</translation>
    </message>
    <message>
        <source>Logical partition (inside the extended partition)</source>
        <translation>Логический раздел (внутри расширенного)</translation>
    </message>
    <message>
        <source>This is the extended partition (container)</source>
        <translation>Это расширенный раздел (контейнер)</translation>
    </message>
    <message>
        <source>There is no free space left.</source>
        <translation>Свободного места не осталось.</translation>
    </message>
    <message>
        <source>Enter a size such as 512M, 20G or max.</source>
        <translation>Введите размер, например 512M, 20G или max.</translation>
    </message>
    <message>
        <source>The size must be a multiple of %1 bytes.</source>
        <translation>Размер должен быть кратен %1 байт.</translation>
    </message>
    <message>
        <source>Delete Partition</source>
        <translation>Удаление раздела</translation>
    </message>
    <message>
        <source>Remove partition %1 from the staged table? The data stays on disk until overwritten, but becomes unreachable once the change is applied.</source>
        <translation>Убрать раздел %1 из накопленной таблицы? Данные останутся на диске до перезаписи, но после применения изменения станут недоступны.</translation>
    </message>
    <message>
        <source>Partition %1 Properties</source>
        <translation>Свойства раздела %1</translation>
    </message>
    <message>
        <source>(unchanged: %1)</source>
        <translation>(без изменений: %1)</translation>
    </message>
    <message>
        <source>Properties</source>
        <translation>Свойства</translation>
    </message>
    <message>
        <source>Resize Partition %1</source>
        <translation>Изменение размера раздела %1</translation>
    </message>
    <message>
        <source>Start LBA:</source>
        <translation>Начальный LBA:</translation>
    </message>
    <message>
        <source>End LBA:</source>
        <translation>Конечный LBA:</translation>
    </message>
    <message>
        <source>Only the table changes. The file system and its data are NOT moved or resized; moving the start makes existing data unreadable through this partition.</source>
        <translation>Меняется только таблица. Файловая система и её данные НЕ перемещаются и не меняют размер; перенос начала раздела делает существующие данные нечитаемыми через этот раздел.</translation>
    </message>
    <message>
        <source>Resize</source>
        <translation>Изменить размер</translation>
    </message>
    <message>
        <source>Enter both LBAs as numbers.</source>
        <translation>Введите оба LBA числами.</translation>
    </message>
    <message>
        <source>Apply</source>
        <translation>Применить</translation>
    </message>
    <message>
        <source>Rewrite the %1 partition table:
%2</source>
        <translation>Перезапись таблицы разделов %1:
%2</translation>
    </message>
    <message>
        <source>Confirm Partition Table Change</source>
        <translation>Подтверждение изменения таблицы разделов</translation>
    </message>
    <message>
        <source>Partition edit refused: %1</source>
        <translation>Изменение разделов отклонено: %1</translation>
    </message>
    <message>
        <source>Writing the partition table failed: %1</source>
        <translation>Не удалось записать таблицу разделов: %1</translation>
    </message>
    <message>
        <source>Writing the partition table failed:
%1

The table on disk may be inconsistent. Re-read it before doing anything else.</source>
        <translation>Не удалось записать таблицу разделов:
%1

Таблица на диске может быть несогласованной. Прежде чем делать что-либо ещё, перечитайте её.</translation>
    </message>
    <message>
        <source>Partition table written and verified (%1 region(s))</source>
        <translation>Таблица разделов записана и проверена (областей: %1)</translation>
    </message>
    <message>
        <source>Format Partition %1</source>
        <translation>Форматирование раздела %1</translation>
    </message>
    <message>
        <source>File system:</source>
        <translation>Файловая система:</translation>
    </message>
    <message>
        <source>Label:</source>
        <translation>Метка:</translation>
    </message>
    <message>
        <source>Automatic</source>
        <translation>Автоматически</translation>
    </message>
    <message>
        <source>Cluster size:</source>
        <translation>Размер кластера:</translation>
    </message>
    <message>
        <source>Quick format: only file system structures are written. Existing data stays recoverable until overwritten - use the ERASE tab first if that matters. NTFS, ext4 and others are not available here.</source>
        <translation>Быстрое форматирование: записываются только структуры файловой системы. Существующие данные остаются восстановимыми до перезаписи — если это важно, сначала воспользуйтесь вкладкой СТИРАНИЕ. NTFS, ext4 и другие здесь недоступны.</translation>
    </message>
    <message>
        <source>Format partition %1 as %2%3</source>
        <translation>Форматирование раздела %1 как %2%3</translation>
    </message>
    <message>
        <source> (label %1)</source>
        <translation> (метка %1)</translation>
    </message>
    <message>
        <source>Confirm Format</source>
        <translation>Подтверждение форматирования</translation>
    </message>
    <message>
        <source>Format refused: %1</source>
        <translation>Форматирование отклонено: %1</translation>
    </message>
    <message>
        <source>Format</source>
        <translation>Форматирование</translation>
    </message>
    <message>
        <source>Format started: %1</source>
        <translation>Форматирование начато: %1</translation>
    </message>
    <message>
        <source>Writing file system structures...</source>
        <translation>Запись структур файловой системы...</translation>
    </message>
    <message>
        <source>Cancel</source>
        <translation>Отмена</translation>
    </message>
    <message>
        <source>Format finished (%1)</source>
        <translation>Форматирование завершено (%1)</translation>
    </message>
    <message>
        <source>verified</source>
        <translation>проверено</translation>
    </message>
    <message>
        <source>not verified</source>
        <translation>не проверено</translation>
    </message>
    <message>
        <source>Format %1: %2</source>
        <translation>Форматирование, %1: %2</translation>
    </message>
    <message>
        <source>Format complete.</source>
        <translation>Форматирование завершено.</translation>
    </message>
    <message>
        <source>Format did not complete: %1</source>
        <translation>Форматирование не завершено: %1</translation>
    </message>
</context>
<context>
    <name>platter::gui::PartitionsTab</name>
    <message>
        <source>&amp;Read Partition Table</source>
        <translation>&amp;Прочитать таблицу разделов</translation>
    </message>
    <message>
        <source>Reads LBA 0-33 and the backup GPT (read-only)</source>
        <translation>Читает LBA 0–33 и резервную GPT (только чтение)</translation>
    </message>
    <message>
        <source>&amp;Edit Partitions...</source>
        <translation>&amp;Изменить разделы...</translation>
    </message>
    <message>
        <source>Create or change the partition table and format partitions. Nothing is written without a typed confirmation.</source>
        <translation>Создание и изменение таблицы разделов, форматирование разделов. Без подтверждения вводом текста ничего не записывается.</translation>
    </message>
    <message>
        <source>#</source>
        <translation>№</translation>
    </message>
    <message>
        <source>Start LBA</source>
        <translation>Начальный LBA</translation>
    </message>
    <message>
        <source>End LBA</source>
        <translation>Конечный LBA</translation>
    </message>
    <message>
        <source>Sectors</source>
        <translation>Секторы</translation>
    </message>
    <message>
        <source>Size</source>
        <translation>Размер</translation>
    </message>
    <message>
        <source>Type</source>
        <translation>Тип</translation>
    </message>
    <message>
        <source>Name / Flags</source>
        <translation>Имя / флаги</translation>
    </message>
    <message>
        <source>Aligned</source>
        <translation>Выравнивание</translation>
    </message>
    <message>
        <source>Validation</source>
        <translation>Проверка</translation>
    </message>
    <message>
        <source>Reading...</source>
        <translation>Чтение...</translation>
    </message>
    <message>
        <source>No device selected.</source>
        <translation>Устройство не выбрано.</translation>
    </message>
    <message>
        <source>Not read yet. Reading sectors from a physical disk may require elevated privileges.</source>
        <translation>Пока не прочитано. Для чтения секторов физического диска могут понадобиться повышенные права.</translation>
    </message>
    <message>
        <source>Scheme: %1</source>
        <translation>Схема: %1</translation>
    </message>
    <message>
        <source>   Disk GUID: %1</source>
        <translation>   GUID диска: %1</translation>
    </message>
    <message>
        <source>   (from BACKUP GPT)</source>
        <translation>   (из РЕЗЕРВНОЙ GPT)</translation>
    </message>
    <message>
        <source>   Disk signature: %1</source>
        <translation>   Сигнатура диска: %1</translation>
    </message>
    <message>
        <source>   %1 partitions</source>
        <translation>   разделов: %1</translation>
    </message>
    <message>
        <source>boot</source>
        <translation>загрузочный</translation>
    </message>
    <message>
        <source>extended</source>
        <translation>расширенный</translation>
    </message>
    <message>
        <source>logical</source>
        <translation>логический</translation>
    </message>
    <message>
        <source>1 MiB</source>
        <translation>1 МиБ</translation>
    </message>
    <message>
        <source>NO</source>
        <translation>НЕТ</translation>
    </message>
    <message>
        <source>No problems found.</source>
        <translation>Проблем не найдено.</translation>
    </message>
</context>
<context>
    <name>platter::gui::PlannedTab</name>
    <message>
        <source>%1

Planned for %2. This tab is a placeholder so the layout matches the specification; no operation can be started from it.</source>
        <translation>%1

Запланировано на %2. Эта вкладка — заглушка, чтобы компоновка соответствовала спецификации; запустить отсюда операцию нельзя.</translation>
    </message>
</context>
<context>
    <name>platter::gui::PreferencesDialog</name>
    <message>
        <source>Preferences</source>
        <translation>Параметры</translation>
    </message>
    <message>
        <source>Record finished benchmarks, scans, SMART queries, erases, images and qualifications in the history</source>
        <translation>Сохранять в истории завершённые тесты, сканирования, запросы SMART, стирания, образы и квалификации</translation>
    </message>
    <message>
        <source>Start the privileged helper when platter starts (Linux)</source>
        <translation>Запускать привилегированный помощник при старте platter (Linux)</translation>
    </message>
    <message>
        <source>The privileged helper is not available on this platform</source>
        <translation>Привилегированный помощник на этой платформе недоступен</translation>
    </message>
    <message>
        <source>Language:</source>
        <translation>Язык:</translation>
    </message>
    <message>
        <source>The language changes the next time platter starts.</source>
        <translation>Язык сменится при следующем запуске platter.</translation>
    </message>
    <message>
        <source>Serial numbers are always hidden when platter starts; turn them on from Settings &gt; Show Serial Numbers when you need them. That choice is deliberately not remembered.</source>
        <translation>При запуске platter серийные номера всегда скрыты; включайте их в меню «Настройки» &gt; «Показывать серийные номера», когда они нужны. Этот выбор намеренно не запоминается.</translation>
    </message>
    <message>
        <source>General</source>
        <translation>Общие</translation>
    </message>
    <message>
        <source>Classic (Windows 2000 style)</source>
        <translation>Классический (в стиле Windows 2000)</translation>
    </message>
    <message>
        <source>Platform default</source>
        <translation>Стиль платформы</translation>
    </message>
    <message>
        <source>Look:</source>
        <translation>Оформление:</translation>
    </message>
    <message>
        <source>Show the toolbar</source>
        <translation>Показывать панель инструментов</translation>
    </message>
    <message>
        <source>Show the status bar</source>
        <translation>Показывать строку состояния</translation>
    </message>
    <message>
        <source>Interface</source>
        <translation>Интерфейс</translation>
    </message>
    <message>
        <source>(none) - the log is kept in memory only</source>
        <translation>(нет) — журнал хранится только в памяти</translation>
    </message>
    <message>
        <source>Browse...</source>
        <translation>Обзор...</translation>
    </message>
    <message>
        <source>Append log to file:</source>
        <translation>Дописывать журнал в файл:</translation>
    </message>
    <message>
        <source>One line per event: time, level, device, message. Serial numbers are not written.</source>
        <translation>Одна строка на событие: время, уровень, устройство, сообщение. Серийные номера не записываются.</translation>
    </message>
    <message>
        <source>Logging</source>
        <translation>Журналирование</translation>
    </message>
    <message>
        <source>automatic: pkexec or sudo + platter-helper</source>
        <translation>автоматически: pkexec или sudo + platter-helper</translation>
    </message>
    <message>
        <source>Helper command:</source>
        <translation>Команда помощника:</translation>
    </message>
    <message>
        <source>default location</source>
        <translation>расположение по умолчанию</translation>
    </message>
    <message>
        <source>History database:</source>
        <translation>База истории:</translation>
    </message>
    <message>
        <source>The history database location applies after restarting platter. $PLATTER_HISTORY_DB, if set, wins.</source>
        <translation>Расположение базы истории применяется после перезапуска platter. Если задана переменная $PLATTER_HISTORY_DB, она имеет приоритет.</translation>
    </message>
    <message>
        <source>Reset All Settings...</source>
        <translation>Сбросить все настройки...</translation>
    </message>
    <message>
        <source>Advanced</source>
        <translation>Дополнительно</translation>
    </message>
    <message>
        <source>Log file</source>
        <translation>Файл журнала</translation>
    </message>
    <message>
        <source>Log files (*.log *.txt);;All files (*)</source>
        <translation>Файлы журнала (*.log *.txt);;Все файлы (*)</translation>
    </message>
    <message>
        <source>History database</source>
        <translation>База истории</translation>
    </message>
    <message>
        <source>SQLite database (*.db);;All files (*)</source>
        <translation>База данных SQLite (*.db);;Все файлы (*)</translation>
    </message>
    <message>
        <source>Reset All Settings</source>
        <translation>Сброс всех настроек</translation>
    </message>
    <message>
        <source>Forget every saved setting (window layout, tab options, preferences)? platter returns to its defaults the next time it starts. Saved history is not touched.</source>
        <translation>Забыть все сохранённые настройки (расположение окон, параметры вкладок, предпочтения)? При следующем запуске platter вернётся к значениям по умолчанию. Сохранённая история не затрагивается.</translation>
    </message>
    <message>
        <source>Done. Restart platter to start from the defaults.</source>
        <translation>Готово. Перезапустите platter, чтобы начать с настроек по умолчанию.</translation>
    </message>
</context>
<context>
    <name>platter::gui::QualifyTab</name>
    <message>
        <source>&amp;Qualify...</source>
        <translation>&amp;Квалификация...</translation>
    </message>
    <message>
        <source>S&amp;top</source>
        <translation>С&amp;топ</translation>
    </message>
    <message>
        <source>&amp;Save Report...</source>
        <translation>&amp;Сохранить отчёт...</translation>
    </message>
    <message>
        <source>Qualification Settings</source>
        <translation>Параметры квалификации</translation>
    </message>
    <message>
        <source>Pass 1 writes a random stream, pass 2 its complement, pass 3 a new stream, pass 4 its complement</source>
        <translation>Проход 1 пишет случайный поток, проход 2 — его дополнение, проход 3 — новый поток, проход 4 — его дополнение</translation>
    </message>
    <message>
        <source>Write passes:</source>
        <translation>Проходов записи:</translation>
    </message>
    <message>
        <source>Read back after every pass</source>
        <translation>Читать обратно после каждого прохода</translation>
    </message>
    <message>
        <source>Final sequential read</source>
        <translation>Итоговое последовательное чтение</translation>
    </message>
    <message>
        <source>off</source>
        <translation>выкл.</translation>
    </message>
    <message>
        <source>Random reads:</source>
        <translation>Случайных чтений:</translation>
    </message>
    <message>
        <source> ms</source>
        <translation> мс</translation>
    </message>
    <message>
        <source>A 1 MiB request taking at least this long counts as slow</source>
        <translation>Запрос в 1 МиБ, занявший не менее этого времени, считается медленным</translation>
    </message>
    <message>
        <source>Slow block at:</source>
        <translation>Медленный блок от:</translation>
    </message>
    <message>
        <source>Qualification OVERWRITES THE WHOLE DEVICE, several times. Use it on a drive you are about to put into service, not on one holding data. The verdict lists every reason; it is an observation of this run, not a prediction.</source>
        <translation>Квалификация несколько раз ПЕРЕЗАПИСЫВАЕТ ВСЁ устройство. Применяйте её к накопителю, который только предстоит ввести в эксплуатацию, а не к тому, где лежат данные. Заключение перечисляет все причины; это наблюдение за данным прогоном, а не прогноз.</translation>
    </message>
    <message>
        <source>Stage</source>
        <translation>Этап</translation>
    </message>
    <message>
        <source>Done</source>
        <translation>Выполнено</translation>
    </message>
    <message>
        <source>Avg</source>
        <translation>Средн.</translation>
    </message>
    <message>
        <source>Min / p99</source>
        <translation>Мин. / p99</translation>
    </message>
    <message>
        <source>Slow</source>
        <translation>Медленных</translation>
    </message>
    <message>
        <source>Errors</source>
        <translation>Ошибки</translation>
    </message>
    <message>
        <source>Mismatch</source>
        <translation>Расхождения</translation>
    </message>
    <message>
        <source>The qualification report appears here when the run ends.</source>
        <translation>Отчёт о квалификации появится здесь по окончании прогона.</translation>
    </message>
    <message>
        <source>No device selected.</source>
        <translation>Устройство не выбрано.</translation>
    </message>
    <message>
        <source>Ready. The SMART data already read for this device is the baseline.</source>
        <translation>Готово. Базой для сравнения служат уже прочитанные данные SMART этого устройства.</translation>
    </message>
    <message>
        <source>Ready. Read SMART first (SMART tab) to include a before/after comparison.</source>
        <translation>Готово. Чтобы получить сравнение «до/после», сначала прочитайте SMART (вкладка SMART).</translation>
    </message>
    <message numerus="yes">
        <source>%n write pass(es)</source>
        <translation>
            <numerusform>%n проход записи</numerusform>
            <numerusform>%n прохода записи</numerusform>
            <numerusform>%n проходов записи</numerusform>
        </translation>
    </message>
    <message>
        <source> with read-back</source>
        <translation> с контрольным чтением</translation>
    </message>
    <message>
        <source>, final read</source>
        <translation>, итоговое чтение</translation>
    </message>
    <message numerus="yes">
        <source>, %n random read(s)</source>
        <translation>
            <numerusform>, %n случайное чтение</numerusform>
            <numerusform>, %n случайных чтения</numerusform>
            <numerusform>, %n случайных чтений</numerusform>
        </translation>
    </message>
    <message>
        <source>Confirm Qualification</source>
        <translation>Подтверждение квалификации</translation>
    </message>
    <message>
        <source>Qualification refused: %1</source>
        <translation>Квалификация отклонена: %1</translation>
    </message>
    <message>
        <source>Qualification</source>
        <translation>Квалификация</translation>
    </message>
    <message>
        <source>Invalid qualification settings: %1</source>
        <translation>Недопустимые параметры квалификации: %1</translation>
    </message>
    <message>
        <source>Qualification started: %1</source>
        <translation>Квалификация начата: %1</translation>
    </message>
    <message>
        <source>Stopping after the current request...</source>
        <translation>Остановка после текущего запроса...</translation>
    </message>
    <message>
        <source>yes</source>
        <translation>да</translation>
    </message>
    <message>
        <source>running</source>
        <translation>выполняется</translation>
    </message>
    <message>
        <source>p50 %1 ms</source>
        <translation>p50 %1 мс</translation>
    </message>
    <message>
        <source>p99 %1 ms</source>
        <translation>p99 %1 мс</translation>
    </message>
    <message>
        <source>%1 MB/s</source>
        <translation>%1 МБ/с</translation>
    </message>
    <message>
        <source>Stage %1/%2  %3 %  %4 MB/s  errors %5</source>
        <translation>Этап %1/%2  %3 %  %4 МБ/с  ошибок %5</translation>
    </message>
    <message>
        <source>Reading SMART again...</source>
        <translation>Повторное чтение SMART...</translation>
    </message>
    <message>
        <source>Qualification finished: %1</source>
        <translation>Квалификация завершена: %1</translation>
    </message>
    <message>
        <source>Could not save to history: %1</source>
        <translation>Не удалось сохранить в историю: %1</translation>
    </message>
    <message>
        <source>Save Qualification Report</source>
        <translation>Сохранение отчёта о квалификации</translation>
    </message>
    <message>
        <source>JSON report (*.json);;Text report (*.txt)</source>
        <translation>Отчёт JSON (*.json);;Текстовый отчёт (*.txt)</translation>
    </message>
    <message>
        <source>Cannot write %1:
%2</source>
        <translation>Не удаётся записать %1:
%2</translation>
    </message>
</context>
<context>
    <name>platter::gui::SmartTab</name>
    <message>
        <source>&amp;Query SMART</source>
        <translation>&amp;Запросить SMART</translation>
    </message>
    <message>
        <source>Runs &apos;smartctl --json -a&apos; (read-only)</source>
        <translation>Запускает «smartctl --json -a» (только чтение)</translation>
    </message>
    <message>
        <source>Health:</source>
        <translation>Состояние:</translation>
    </message>
    <message>
        <source>ID</source>
        <translation>ID</translation>
    </message>
    <message>
        <source>Attribute</source>
        <translation>Атрибут</translation>
    </message>
    <message>
        <source>Current</source>
        <translation>Текущее</translation>
    </message>
    <message>
        <source>Worst</source>
        <translation>Худшее</translation>
    </message>
    <message>
        <source>Threshold</source>
        <translation>Порог</translation>
    </message>
    <message>
        <source>Raw (dec)</source>
        <translation>Сырое (дес.)</translation>
    </message>
    <message>
        <source>Raw (hex)</source>
        <translation>Сырое (hex)</translation>
    </message>
    <message>
        <source>Status</source>
        <translation>Статус</translation>
    </message>
    <message>
        <source>Assessment details</source>
        <translation>Подробности оценки</translation>
    </message>
    <message>
        <source>Querying smartctl...</source>
        <translation>Опрос smartctl...</translation>
    </message>
    <message>
        <source>UNKNOWN</source>
        <translation>НЕИЗВЕСТНО</translation>
    </message>
    <message>
        <source>No device selected.</source>
        <translation>Устройство не выбрано.</translation>
    </message>
    <message>
        <source>Image files have no SMART data.</source>
        <translation>У файлов образов нет данных SMART.</translation>
    </message>
    <message>
        <source>Not queried yet.</source>
        <translation>Пока не опрашивалось.</translation>
    </message>
    <message>
        <source>smartctl %1 (exit status %2), %3%4</source>
        <translation>smartctl %1 (код завершения %2), %3%4</translation>
    </message>
    <message>
        <source>, %1 °C</source>
        <translation>, %1 °C</translation>
    </message>
    <message>
        <source>FAILING NOW</source>
        <translation>СБОЙ СЕЙЧАС</translation>
    </message>
    <message>
        <source>Failed in past</source>
        <translation>Сбой в прошлом</translation>
    </message>
    <message>
        <source>OK</source>
        <translation>ОК</translation>
    </message>
    <message>
        <source>-</source>
        <translation>-</translation>
    </message>
    <message>
        <source>Critical Warning</source>
        <translation>Критическое предупреждение</translation>
    </message>
    <message>
        <source>Temperature</source>
        <translation>Температура</translation>
    </message>
    <message>
        <source>Available Spare</source>
        <translation>Доступный резерв</translation>
    </message>
    <message>
        <source>Available Spare Threshold</source>
        <translation>Порог доступного резерва</translation>
    </message>
    <message>
        <source>Percentage Used</source>
        <translation>Использовано ресурса</translation>
    </message>
    <message>
        <source>Data Units Read</source>
        <translation>Прочитано единиц данных</translation>
    </message>
    <message>
        <source>Data Units Written</source>
        <translation>Записано единиц данных</translation>
    </message>
    <message>
        <source>Power Cycles</source>
        <translation>Циклов включения</translation>
    </message>
    <message>
        <source>Power On Hours</source>
        <translation>Часов работы</translation>
    </message>
    <message>
        <source>Unsafe Shutdowns</source>
        <translation>Нештатных выключений</translation>
    </message>
    <message>
        <source>Media and Data Integrity Errors</source>
        <translation>Ошибки носителя и целостности данных</translation>
    </message>
    <message>
        <source>Error Information Log Entries</source>
        <translation>Записей журнала ошибок</translation>
    </message>
</context>
<context>
    <name>platter::gui::SurfaceMap</name>
    <message>
        <source>No scan data</source>
        <translation>Нет данных сканирования</translation>
    </message>
</context>
<context>
    <name>platter::gui::SurfaceTab</name>
    <message>
        <source>&amp;Start</source>
        <translation>&amp;Старт</translation>
    </message>
    <message>
        <source>&amp;Pause</source>
        <translation>&amp;Пауза</translation>
    </message>
    <message>
        <source>S&amp;top</source>
        <translation>С&amp;топ</translation>
    </message>
    <message>
        <source>Open Scan...</source>
        <translation>Открыть скан...</translation>
    </message>
    <message>
        <source>&amp;Export...</source>
        <translation>&amp;Экспорт...</translation>
    </message>
    <message>
        <source>Start a READ surface scan (never writes)</source>
        <translation>Запустить сканирование поверхности ЧТЕНИЕМ (ничего не записывает)</translation>
    </message>
    <message>
        <source>Load a saved JSON scan for viewing or comparison</source>
        <translation>Загрузить сохранённый JSON-скан для просмотра или сравнения</translation>
    </message>
    <message>
        <source>Scan Settings</source>
        <translation>Параметры сканирования</translation>
    </message>
    <message>
        <source>READ</source>
        <translation>ЧТЕНИЕ</translation>
    </message>
    <message>
        <source>WRITE (destructive)</source>
        <translation>ЗАПИСЬ (деструктивно)</translation>
    </message>
    <message>
        <source>WRITE + READBACK (destructive)</source>
        <translation>ЗАПИСЬ + КОНТРОЛЬНОЕ ЧТЕНИЕ (деструктивно)</translation>
    </message>
    <message>
        <source>CUSTOM PATTERN (destructive)</source>
        <translation>СВОЙ ШАБЛОН (деструктивно)</translation>
    </message>
    <message>
        <source>Mode:</source>
        <translation>Режим:</translation>
    </message>
    <message>
        <source>hex bytes, e.g. DEADBEEF</source>
        <translation>байты в hex, например DEADBEEF</translation>
    </message>
    <message>
        <source>Repeating byte pattern for the write modes (default A5 5A)</source>
        <translation>Повторяющийся байтовый шаблон для режимов записи (по умолчанию A5 5A)</translation>
    </message>
    <message>
        <source>Pattern:</source>
        <translation>Шаблон:</translation>
    </message>
    <message>
        <source>end of device</source>
        <translation>конец устройства</translation>
    </message>
    <message>
        <source>Start LBA:</source>
        <translation>Начальный LBA:</translation>
    </message>
    <message>
        <source>End LBA:</source>
        <translation>Конечный LBA:</translation>
    </message>
    <message>
        <source>Block size:</source>
        <translation>Размер блока:</translation>
    </message>
    <message>
        <source>Forward</source>
        <translation>Вперёд</translation>
    </message>
    <message>
        <source>Reverse</source>
        <translation>Назад</translation>
    </message>
    <message>
        <source>Random</source>
        <translation>Случайное</translation>
    </message>
    <message>
        <source>Sampled</source>
        <translation>Выборочно</translation>
    </message>
    <message>
        <source>Order:</source>
        <translation>Порядок:</translation>
    </message>
    <message>
        <source>Sample:</source>
        <translation>Выборка:</translation>
    </message>
    <message>
        <source>Retries:</source>
        <translation>Повторы:</translation>
    </message>
    <message>
        <source>Continue</source>
        <translation>Продолжать</translation>
    </message>
    <message>
        <source>Stop after</source>
        <translation>Остановиться после</translation>
    </message>
    <message>
        <source>On errors:</source>
        <translation>При ошибках:</translation>
    </message>
    <message>
        <source>Direct I/O (bypass OS cache)</source>
        <translation>Прямой ввод-вывод (в обход кэша ОС)</translation>
    </message>
    <message>
        <source>above</source>
        <translation>свыше</translation>
    </message>
    <message>
        <source>read error</source>
        <translation>ошибка чтения</translation>
    </message>
    <message>
        <source>Classes:</source>
        <translation>Классы:</translation>
    </message>
    <message>
        <source>Defaults</source>
        <translation>По умолчанию</translation>
    </message>
    <message>
        <source>Surface Map</source>
        <translation>Карта поверхности</translation>
    </message>
    <message>
        <source>Zoom:</source>
        <translation>Масштаб:</translation>
    </message>
    <message>
        <source>Fit</source>
        <translation>Вписать</translation>
    </message>
    <message>
        <source>%1 block(s)/cell</source>
        <translation>блоков на ячейку: %1</translation>
    </message>
    <message>
        <source>Click a cell for details. Ctrl+wheel zooms.</source>
        <translation>Щёлкните ячейку для подробностей. Ctrl+колесо — масштаб.</translation>
    </message>
    <message>
        <source>untested</source>
        <translation>не проверено</translation>
    </message>
    <message>
        <source>fast</source>
        <translation>быстро</translation>
    </message>
    <message>
        <source>good</source>
        <translation>хорошо</translation>
    </message>
    <message>
        <source>medium</source>
        <translation>средне</translation>
    </message>
    <message>
        <source>slow</source>
        <translation>медленно</translation>
    </message>
    <message>
        <source>very slow</source>
        <translation>очень медленно</translation>
    </message>
    <message>
        <source>error</source>
        <translation>ошибка</translation>
    </message>
    <message>
        <source>Statistics</source>
        <translation>Статистика</translation>
    </message>
    <message>
        <source>Tested:</source>
        <translation>Проверено:</translation>
    </message>
    <message>
        <source>Requests:</source>
        <translation>Запросов:</translation>
    </message>
    <message>
        <source>Failed:</source>
        <translation>Сбоев:</translation>
    </message>
    <message>
        <source>Slow:</source>
        <translation>Медленных:</translation>
    </message>
    <message>
        <source>Latency min:</source>
        <translation>Задержка, мин.:</translation>
    </message>
    <message>
        <source>Latency mean:</source>
        <translation>Задержка, средняя:</translation>
    </message>
    <message>
        <source>Latency median:</source>
        <translation>Задержка, медиана:</translation>
    </message>
    <message>
        <source>Latency P95:</source>
        <translation>Задержка, P95:</translation>
    </message>
    <message>
        <source>Latency P99:</source>
        <translation>Задержка, P99:</translation>
    </message>
    <message>
        <source>Latency max:</source>
        <translation>Задержка, макс.:</translation>
    </message>
    <message>
        <source>Current speed:</source>
        <translation>Текущая скорость:</translation>
    </message>
    <message>
        <source>Average speed:</source>
        <translation>Средняя скорость:</translation>
    </message>
    <message>
        <source>Elapsed:</source>
        <translation>Прошло:</translation>
    </message>
    <message>
        <source>Remaining:</source>
        <translation>Осталось:</translation>
    </message>
    <message>
        <source>Current LBA:</source>
        <translation>Текущий LBA:</translation>
    </message>
    <message>
        <source>Selected Cell</source>
        <translation>Выбранная ячейка</translation>
    </message>
    <message>
        <source>Block(s):</source>
        <translation>Блоки:</translation>
    </message>
    <message>
        <source>LBA range:</source>
        <translation>Диапазон LBA:</translation>
    </message>
    <message>
        <source>Transfer size:</source>
        <translation>Размер передачи:</translation>
    </message>
    <message>
        <source>Slowest read:</source>
        <translation>Самое медленное чтение:</translation>
    </message>
    <message>
        <source>Throughput:</source>
        <translation>Пропускная способность:</translation>
    </message>
    <message>
        <source>Errors:</source>
        <translation>Ошибки:</translation>
    </message>
    <message>
        <source>Tested at:</source>
        <translation>Проверено:</translation>
    </message>
    <message>
        <source>end (%1)</source>
        <translation>конец (%1)</translation>
    </message>
    <message>
        <source>Ready to scan %1 (read-only).</source>
        <translation>Готово к сканированию: %1 (только чтение).</translation>
    </message>
    <message>
        <source>No device selected.</source>
        <translation>Устройство не выбрано.</translation>
    </message>
    <message>
        <source>The pattern needs an even number of hex digits.</source>
        <translation>В шаблоне должно быть чётное число hex-цифр.</translation>
    </message>
    <message>
        <source>Enter the pattern to write.</source>
        <translation>Введите шаблон для записи.</translation>
    </message>
    <message>
        <source>Start LBA is not a number.</source>
        <translation>Начальный LBA не является числом.</translation>
    </message>
    <message>
        <source>End LBA is not a number.</source>
        <translation>Конечный LBA не является числом.</translation>
    </message>
    <message>
        <source>Latency class bounds must increase from top to bottom.</source>
        <translation>Границы классов задержки должны возрастать сверху вниз.</translation>
    </message>
    <message>
        <source>&amp;Resume</source>
        <translation>&amp;Продолжить</translation>
    </message>
    <message>
        <source>Surface Scan</source>
        <translation>Сканирование поверхности</translation>
    </message>
    <message>
        <source>Enter an End LBA too, or leave both Start and End at their defaults for the whole device.</source>
        <translation>Укажите также конечный LBA или оставьте оба значения по умолчанию, чтобы проверить всё устройство.</translation>
    </message>
    <message>
        <source>Confirm Write Scan</source>
        <translation>Подтверждение сканирования с записью</translation>
    </message>
    <message>
        <source>%1 surface scan</source>
        <translation>Сканирование поверхности: %1</translation>
    </message>
    <message>
        <source>Write scan refused: %1</source>
        <translation>Сканирование с записью отклонено: %1</translation>
    </message>
    <message>
        <source>Write scan authorized (destroys data in the chosen range)</source>
        <translation>Сканирование с записью разрешено (уничтожает данные в выбранном диапазоне)</translation>
    </message>
    <message>
        <source>Cannot open device for scanning: %1</source>
        <translation>Не удаётся открыть устройство для сканирования: %1</translation>
    </message>
    <message>
        <source>Cannot open %1 for reading:
%2%3</source>
        <translation>Не удаётся открыть %1 для чтения:
%2%3</translation>
    </message>
    <message>
        <source>

Scanning a physical disk needs root / Administrator.</source>
        <translation>

Для сканирования физического диска нужны права root / администратора.</translation>
    </message>
    <message>
        <source>Invalid scan settings: %1</source>
        <translation>Недопустимые параметры сканирования: %1</translation>
    </message>
    <message>
        <source>Surface scan started (%6, %1 blocks of %2 bytes, LBA %3-%4, %5)</source>
        <translation>Сканирование поверхности запущено (%6, блоков: %1 по %2 байт, LBA %3–%4, %5)</translation>
    </message>
    <message>
        <source>direct I/O</source>
        <translation>прямой ввод-вывод</translation>
    </message>
    <message>
        <source>buffered I/O</source>
        <translation>буферизованный ввод-вывод</translation>
    </message>
    <message>
        <source>Scanning %1 ...</source>
        <translation>Сканирование: %1 ...</translation>
    </message>
    <message>
        <source>Surface scan resumed</source>
        <translation>Сканирование поверхности продолжено</translation>
    </message>
    <message>
        <source>Surface scan paused</source>
        <translation>Сканирование поверхности приостановлено</translation>
    </message>
    <message>
        <source>Stopping after the current request...</source>
        <translation>Остановка после текущего запроса...</translation>
    </message>
    <message>
        <source>Paused at %1 %.</source>
        <translation>Приостановлено на %1 %.</translation>
    </message>
    <message>
        <source>Scanning %1 ...  %2 %</source>
        <translation>Сканирование: %1 ...  %2 %</translation>
    </message>
    <message>
        <source>%1: %2 requests, %3 failed, %4 slow, %5 average</source>
        <translation>%1: запросов %2, сбоев %3, медленных %4, в среднем %5</translation>
    </message>
    <message>
        <source>Surface scan finished: %1</source>
        <translation>Сканирование поверхности завершено: %1</translation>
    </message>
    <message>
        <source>Scan not saved to history: %1</source>
        <translation>Скан не сохранён в истории: %1</translation>
    </message>
    <message>
        <source>not tested</source>
        <translation>не проверено</translation>
    </message>
    <message>
        <source>none</source>
        <translation>нет</translation>
    </message>
    <message>
        <source>Open Scan</source>
        <translation>Открыть скан</translation>
    </message>
    <message>
        <source>Cannot read %1:
%2</source>
        <translation>Не удаётся прочитать %1:
%2</translation>
    </message>
    <message>
        <source>Cannot load scan: %1</source>
        <translation>Не удаётся загрузить скан: %1</translation>
    </message>
    <message>
        <source>Viewing saved scan of %1 (%2, %3)</source>
        <translation>Просмотр сохранённого скана %1 (%2, %3)</translation>
    </message>
    <message>
        <source>Loaded saved scan %1</source>
        <translation>Загружен сохранённый скан %1</translation>
    </message>
    <message>
        <source>platter scan (*.json);;All files (*)</source>
        <translation>Скан platter (*.json);;Все файлы (*)</translation>
    </message>
    <message>
        <source>platter scan, JSON (*.json)</source>
        <translation>Скан platter, JSON (*.json)</translation>
    </message>
    <message>
        <source>Per-block CSV (*.csv)</source>
        <translation>CSV по блокам (*.csv)</translation>
    </message>
    <message>
        <source>Text report (*.txt)</source>
        <translation>Текстовый отчёт (*.txt)</translation>
    </message>
    <message>
        <source>Export Scan</source>
        <translation>Экспорт скана</translation>
    </message>
    <message>
        <source>Cannot write %1:
%2</source>
        <translation>Не удаётся записать %1:
%2</translation>
    </message>
    <message>
        <source>Scan exported to %1 (serial numbers omitted)</source>
        <translation>Скан экспортирован в %1 (серийные номера не включены)</translation>
    </message>
    <message>
        <source> ms</source>
        <translation> мс</translation>
    </message>
</context>
<context>
    <name>units</name>
    <message>
        <source>B</source>
        <translation>Б</translation>
    </message>
    <message>
        <source>KB</source>
        <translation>КБ</translation>
    </message>
    <message>
        <source>MB</source>
        <translation>МБ</translation>
    </message>
    <message>
        <source>GB</source>
        <translation>ГБ</translation>
    </message>
    <message>
        <source>TB</source>
        <translation>ТБ</translation>
    </message>
    <message>
        <source>PB</source>
        <translation>ПБ</translation>
    </message>
    <message>
        <source>KiB</source>
        <translation>КиБ</translation>
    </message>
    <message>
        <source>MiB</source>
        <translation>МиБ</translation>
    </message>
    <message>
        <source>GiB</source>
        <translation>ГиБ</translation>
    </message>
    <message>
        <source>TiB</source>
        <translation>ТиБ</translation>
    </message>
    <message>
        <source>PiB</source>
        <translation>ПиБ</translation>
    </message>
    <message>
        <source>/s</source>
        <translation>/с</translation>
    </message>
</context>
</TS>
