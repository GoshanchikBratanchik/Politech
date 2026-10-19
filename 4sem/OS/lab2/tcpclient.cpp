// tcpclient.cpp - TCP-клиент (ОС Linux)
//
// Запуск:  tcpclient IP:PORT FILE
//
// Клиент читает сообщения из текстового файла FILE (одно сообщение на строке,
// формат "dd.mm.yyyy hh:mm:ss hh:mm:ss Message"), подключается к серверу и
// передает их по протоколу:
//   "put"                                  - 3 байта, один раз после подключения
//   затем для каждого сообщения:
//   [4 байта] номер сообщения (с 0)        - сетевой порядок байт
//   [4 байта] дата  yyyy*10000+mm*100+dd   - сетевой порядок байт
//   [4 байта] время hh*10000+mm*100+ss     - сетевой порядок байт
//   [4 байта] время hh*10000+mm*100+ss     - сетевой порядок байт
//   [4 байта] длина поля Message N         - сетевой порядок байт
//   [N байт ] текст поля Message
// На каждое сообщение сервер отвечает двумя байтами "ok".
//
// Доп. задание: строки, в которых нарушен формат какого-либо поля,
// игнорируются и на сервер не отправляются.
//
// Сокет работает в блокирующем режиме. Чтобы не ждать "ok" после каждого
// сообщения, клиент отправляет сообщения подряд (пачками), а пришедшие
// подтверждения забирает "по пути" (наличие данных проверяется select() с
// нулевым таймаутом). Количество неподтвержденных сообщений ограничено окном
// MAX_UNCONFIRMED. В конце клиент дожидается подтверждения последнего сообщения.
//
// Компиляция: g++ -o tcpclient tcpclient.cpp

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <errno.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <string>
#include <fstream>

#define CONNECT_ATTEMPTS   10      // число попыток подключения
#define CONNECT_DELAY_MS   100     // пауза между попытками, мс
#define MAX_UNCONFIRMED    1000    // окно: сколько сообщений можно отправить без "ok"
#define SEND_CHUNK         65536   // размер пачки данных, после которой выполняется отправка
#define MAX_REPORTED_BAD   20      // сколько некорректных строк печатать подробно

// ---------------------------------------------------------------------------
// Разбор и проверка строк входного файла
// ---------------------------------------------------------------------------

// Сообщение, полученное из строки файла
struct Message
{
    unsigned int date;   // yyyy*10000 + mm*100 + dd
    unsigned int time1;  // hh*10000 + mm*100 + ss
    unsigned int time2;  // hh*10000 + mm*100 + ss
    std::string  text;   // поле Message
};

// Читает cnt десятичных цифр из строки s начиная с позиции pos.
// Возвращает false, если встретился не цифровой символ.
static bool parse_digits(const std::string& s, size_t pos, size_t cnt, unsigned int* out)
{
    unsigned int v = 0;
    size_t i;

    if (pos + cnt > s.size())
        return false;

    for (i = 0; i < cnt; i++)
    {
        char c = s[pos + i];
        if (c < '0' || c > '9')
            return false;
        v = v * 10 + (unsigned int)(c - '0');
    }

    *out = v;
    return true;
}

static bool is_leap_year(unsigned int y)
{
    return (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0);
}

// Разбор даты "dd.mm.yyyy" с позиции pos (10 символов) с проверкой
// существования даты (месяц 1..12, день в пределах месяца, учет високосных лет).
static bool parse_date(const std::string& s, size_t pos, unsigned int* out)
{
    static const unsigned int days_in_month[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    unsigned int d, m, y, max_day;

    if (!parse_digits(s, pos, 2, &d) || s[pos + 2] != '.' ||
        !parse_digits(s, pos + 3, 2, &m) || s[pos + 5] != '.' ||
        !parse_digits(s, pos + 6, 4, &y))
        return false;

    if (m < 1 || m > 12 || d < 1)
        return false;

    max_day = days_in_month[m - 1];
    if (m == 2 && is_leap_year(y))
        max_day = 29;
    if (d > max_day)
        return false;

    *out = y * 10000 + m * 100 + d;
    return true;
}

// Разбор времени "hh:mm:ss" с позиции pos (8 символов): часы 0..23, минуты и секунды 0..59
static bool parse_time(const std::string& s, size_t pos, unsigned int* out)
{
    unsigned int h, m, sec;

    if (!parse_digits(s, pos, 2, &h) || s[pos + 2] != ':' ||
        !parse_digits(s, pos + 3, 2, &m) || s[pos + 5] != ':' ||
        !parse_digits(s, pos + 6, 2, &sec))
        return false;

    if (h > 23 || m > 59 || sec > 59)
        return false;

    *out = h * 10000 + m * 100 + sec;
    return true;
}

// Разбор строки "dd.mm.yyyy hh:mm:ss hh:mm:ss Message".
// Позиции полей фиксированы:
//   0..9   дата, 10 пробел, 11..18 время1, 19 пробел, 20..27 время2, 28 пробел, 29.. текст
// Возвращает false, если формат строки нарушен.
static bool parse_line(const std::string& line, Message* msg)
{
    if (line.size() < 29)
        return false;

    if (!parse_date(line, 0, &msg->date) || line[10] != ' ' ||
        !parse_time(line, 11, &msg->time1) || line[19] != ' ' ||
        !parse_time(line, 20, &msg->time2) || line[28] != ' ')
        return false;

    msg->text = line.substr(29);
    return true;
}

// ---------------------------------------------------------------------------
// Формирование данных протокола
// ---------------------------------------------------------------------------

// Добавляет в буфер 32-битное число в сетевом порядке байт
static void append_u32(std::string& buf, unsigned int value)
{
    unsigned int n = htonl(value);
    buf.append((const char*)&n, 4);
}

// Добавляет в буфер сообщение в формате протокола
static void append_message(std::string& buf, unsigned int index, const Message& msg)
{
    append_u32(buf, index);
    append_u32(buf, msg.date);
    append_u32(buf, msg.time1);
    append_u32(buf, msg.time2);
    append_u32(buf, (unsigned int)msg.text.size());
    buf.append(msg.text);
}

// ---------------------------------------------------------------------------
// Работа с сокетом
// ---------------------------------------------------------------------------

static void sleep_ms(int ms)
{
    usleep(ms * 1000);
}

// Разбор аргумента "IP:PORT" и заполнение адреса сервера
static bool parse_address(const char* arg, struct sockaddr_in* addr)
{
    std::string s(arg);
    size_t colon = s.rfind(':');
    std::string host, port;
    struct addrinfo hints, *res = 0;
    int port_num;

    if (colon == std::string::npos || colon == 0 || colon + 1 >= s.size())
        return false;

    host = s.substr(0, colon);
    port = s.substr(colon + 1);
    port_num = atoi(port.c_str());
    if (port_num <= 0 || port_num > 65535)
        return false;

    memset(addr, 0, sizeof(*addr));
    addr->sin_family = AF_INET;
    addr->sin_port = htons((unsigned short)port_num);

    // Числовой IPv4-адрес
    if (inet_pton(AF_INET, host.c_str(), &addr->sin_addr) == 1)
        return true;

    // Иначе - попытка определить адрес по имени узла
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host.c_str(), 0, &hints, &res) != 0 || !res)
        return false;
    addr->sin_addr = ((struct sockaddr_in*)res->ai_addr)->sin_addr;
    freeaddrinfo(res);
    return true;
}

// Подключение к серверу: до CONNECT_ATTEMPTS попыток с паузой CONNECT_DELAY_MS.
// Возвращает дескриптор сокета или -1.
static int connect_to_server(const struct sockaddr_in* addr)
{
    int attempt;

    for (attempt = 1; attempt <= CONNECT_ATTEMPTS; attempt++)
    {
        // После неудачного connect() состояние сокета не определено,
        // поэтому для каждой попытки создается новый сокет
        int s = socket(AF_INET, SOCK_STREAM, 0);
        if (s < 0)
        {
            printf("socket() error: %s\n", strerror(errno));
            return -1;
        }

        if (connect(s, (const struct sockaddr*)addr, sizeof(*addr)) == 0)
            return s;

        printf("Connect attempt %d failed: %s\n", attempt, strerror(errno));
        close(s);

        if (attempt < CONNECT_ATTEMPTS)
            sleep_ms(CONNECT_DELAY_MS);
    }

    return -1;
}

// Отправка всего буфера (send может передать данные частями)
static bool send_all(int s, const char* data, size_t size)
{
    size_t sent = 0;

    while (sent < size)
    {
        ssize_t r = send(s, data + sent, size - sent, MSG_NOSIGNAL);
        if (r < 0)
        {
            if (errno == EINTR)
                continue;
            printf("send() error: %s\n", strerror(errno));
            return false;
        }
        sent += (size_t)r;
    }

    return true;
}

// Состояние приема подтверждений
struct ConfirmState
{
    unsigned int confirmed;  // сколько "ok" получено
    int          phase;      // 0 - ожидается 'o', 1 - ожидается 'k'
    bool         warned;     // уже сообщали о неверном подтверждении
};

// Прием подтверждений "ok".
// wait == false: забрать только уже пришедшие данные (без блокировки);
// wait == true : дождаться хотя бы одного блока данных от сервера.
// Возвращает false при обрыве соединения или ошибке.
static bool receive_confirmations(int s, bool wait, ConfirmState* st)
{
    char buf[4096];

    for (;;)
    {
        ssize_t r, i;

        if (!wait)
        {
            // Проверка наличия данных во входном буфере сокета без ожидания
            fd_set rfd;
            struct timeval tv = { 0, 0 };
            int sel;

            FD_ZERO(&rfd);
            FD_SET(s, &rfd);
            sel = select(s + 1, &rfd, 0, 0, &tv);
            if (sel < 0)
            {
                if (errno == EINTR)
                    continue;
                printf("select() error: %s\n", strerror(errno));
                return false;
            }
            if (sel == 0)
                return true; // данных нет
        }

        r = recv(s, buf, sizeof(buf), 0);
        if (r < 0)
        {
            if (errno == EINTR)
                continue;
            printf("recv() error: %s\n", strerror(errno));
            return false;
        }
        if (r == 0)
        {
            printf("Connection closed by server\n");
            return false;
        }

        // Каждые два байта "ok" - подтверждение одного сообщения
        for (i = 0; i < r; i++)
        {
            char expected = (st->phase == 0) ? 'o' : 'k';
            if (buf[i] != expected && !st->warned)
            {
                printf("Warning: unexpected confirmation byte 0x%02x\n", (unsigned char)buf[i]);
                st->warned = true;
            }
            if (st->phase == 1)
                st->confirmed++;
            st->phase ^= 1;
        }

        if (wait)
            return true;
    }
}

// ---------------------------------------------------------------------------

int main(int argc, char* argv[])
{
    struct sockaddr_in addr;
    std::string line, out;
    unsigned int line_no = 0, bad_lines = 0;
    unsigned int queued = 0;   // сообщений сформировано (номер следующего сообщения)
    unsigned int sent = 0;     // сообщений фактически передано в сокет
    unsigned int pending = 0;  // сообщений в буфере out, еще не переданных
    bool ok = true, stop_sent = false;
    ConfirmState st;
    int s, one = 1;

    // stdout без буферизации: сообщения не теряются при аварийной остановке
    setvbuf(stdout, 0, _IONBF, 0);

    if (argc < 3)
    {
        printf("Usage: %s IP:PORT FILE\n", argv[0]);
        return 1;
    }

    if (!parse_address(argv[1], &addr))
    {
        printf("Invalid server address: %s\n", argv[1]);
        return 1;
    }

    std::ifstream file(argv[2], std::ios::in | std::ios::binary);
    if (!file.is_open())
    {
        printf("Cannot open file: %s\n", argv[2]);
        return 1;
    }

    // Подключение к серверу
    s = connect_to_server(&addr);
    if (s < 0)
    {
        printf("Error: failed to connect to %s after %d attempts\n", argv[1], CONNECT_ATTEMPTS);
        return 1;
    }
    printf("Connected to %s\n", argv[1]);

    // Отключение алгоритма Нейгла: последняя пачка данных уйдет без задержки
    setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (const char*)&one, sizeof(one));

    st.confirmed = 0;
    st.phase = 0;
    st.warned = false;

    // Команда серверу: далее будут передаваться сообщения
    out = "put";

    while (ok && !stop_sent && std::getline(file, line))
    {
        Message msg;

        line_no++;

        // Удаление символа '\r' (файл может иметь окончания строк Windows)
        if (!line.empty() && line[line.size() - 1] == '\r')
            line.erase(line.size() - 1);

        // Пустые строки игнорируются
        if (line.empty())
            continue;

        // Строки с нарушенным форматом игнорируются (доп. задание)
        if (!parse_line(line, &msg))
        {
            bad_lines++;
            if (bad_lines <= MAX_REPORTED_BAD)
                printf("Line %u: invalid format, skipped\n", line_no);
            continue;
        }

        append_message(out, queued, msg);
        queued++;
        pending++;

        // После сообщения "stop" сервер завершает работу,
        // поэтому дальнейшие сообщения не передаются
        if (msg.text == "stop")
            stop_sent = true;

        // Отправка накопленной пачки
        if (out.size() >= SEND_CHUNK || pending >= 100 || stop_sent)
        {
            if (!send_all(s, out.data(), out.size()))
            {
                ok = false;
                break;
            }
            out.clear();
            sent += pending;
            pending = 0;
        }

        // Забрать уже пришедшие подтверждения (без ожидания)
        if (!receive_confirmations(s, false, &st))
        {
            ok = false;
            break;
        }

        // Окно заполнено - ждать подтверждений
        while (ok && sent - st.confirmed >= MAX_UNCONFIRMED)
            ok = receive_confirmations(s, true, &st);
    }

    // Отправка остатка данных (в т.ч. "put", если сообщений не было)
    if (ok && !out.empty())
    {
        ok = send_all(s, out.data(), out.size());
        if (ok)
        {
            sent += pending;
            pending = 0;
        }
    }

    // Ожидание подтверждения всех (в т.ч. последнего) сообщений
    while (ok && st.confirmed < sent)
        ok = receive_confirmations(s, true, &st);

    close(s);

    if (bad_lines > MAX_REPORTED_BAD)
        printf("... %u invalid lines skipped in total\n", bad_lines);
    if (stop_sent)
        printf("'stop' message sent, the rest of the file is not transmitted\n");

    if (!ok)
    {
        printf("Error: %u of %u message(s) confirmed\n", st.confirmed, sent);
        return 1;
    }

    printf("%u message(s) sent and confirmed\n", st.confirmed);
    return 0;
}
