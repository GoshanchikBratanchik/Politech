// udpclient.cpp - UDP-клиент (ОС Windows)
//
// Запуск:  udpclient IP:PORT FILE
//
// Клиент читает сообщения из файла FILE (формат строки
// "dd.mm.yyyy hh:mm:ss hh:mm:ss Message") и отправляет каждое сообщение
// отдельной дейтаграммой на UDP-сервер. Формат дейтаграммы такой же, как у
// сообщения в TCP-протоколе:
//   [4] номер, [4] дата, [4] время1, [4] время2, [4] длина N, [N] текст
// (числа 32-битные, сетевой порядок байт).
//
// В ответ на каждую дейтаграмму сервер присылает дейтаграмму с номерами
// (4 байта, сетевой порядок) последних (не более 20) принятых сообщений.
//
// Алгоритм:
//   - клиент должен загрузить на сервер хотя бы 20 любых сообщений
//     (если в файле меньше 20 сообщений - все сообщения);
//   - за один проход отправляются только те сообщения, которые, по данным
//     клиента, отсутствуют на сервере (подтвержденные повторно не посылаются);
//   - после отправки клиент принимает ответы; если в течение 100 мс от сервера
//     не пришло ни одной дейтаграммы - неподтвержденные сообщения отправляются
//     повторно;
//   - сообщение с текстом "stop" (сервер по нему завершается) отправляется
//     последним - только после подтверждения остальных нужных сообщений;
//   - если сервер не отвечает в течение MAX_SILENT_ROUNDS повторов подряд,
//     клиент сообщает об ошибке и завершается.
//
// Сокет один, открывается при запуске, работает в блокирующем режиме;
// наличие дейтаграмм во входном буфере проверяется функцией select().
//
// Доп. задание: строки с нарушенным форматом полей игнорируются.
//
// Программа предназначена для Windows (Visual Studio: cl /EHsc udpclient.cpp),
// но для отладки может быть собрана и в Linux (g++ -o udpclient udpclient.cpp).

#ifdef _WIN32
    #define WIN32_LEAN_AND_MEAN
    #define _CRT_SECURE_NO_WARNINGS
    #define _WINSOCK_DEPRECATED_NO_WARNINGS
    #include <winsock2.h>
    #include <windows.h>
    // Директива линковщику: использовать библиотеку сокетов
    #pragma comment(lib, "ws2_32.lib")
    typedef int socklen_t;
    // Отключение ошибки WSAECONNRESET на UDP-сокете (приходит, если сервер
    // недоступен и ОС получила ICMP "port unreachable")
    #ifndef SIO_UDP_CONNRESET
        #define SIO_UDP_CONNRESET _WSAIOW(IOC_VENDOR, 12)
    #endif
#else // LINUX
    #include <sys/types.h>
    #include <sys/socket.h>
    #include <sys/select.h>
    #include <sys/time.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <netdb.h>
    #include <unistd.h>
    #include <errno.h>
    typedef int SOCKET;
    #define INVALID_SOCKET  (-1)
    #define closesocket     close
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <string>
#include <vector>
#include <fstream>

#define REQUIRED_MESSAGES   20      // сколько сообщений нужно загрузить на сервер
#define WAIT_MS             100     // таймаут ожидания ответа, мс
#define MAX_SILENT_ROUNDS   50      // сколько повторов подряд без ответа допустимо
#define MAX_UDP_PAYLOAD     65507   // максимальный размер данных UDP-дейтаграммы
#define RECV_BUF_SIZE       65536
#define MAX_REPORTED_BAD    20

// ---------------------------------------------------------------------------
// Разбор и проверка строк входного файла (как в tcpclient)
// ---------------------------------------------------------------------------

struct Message
{
    unsigned int date;   // yyyy*10000 + mm*100 + dd
    unsigned int time1;  // hh*10000 + mm*100 + ss
    unsigned int time2;  // hh*10000 + mm*100 + ss
    std::string  text;   // поле Message
};

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

// "dd.mm.yyyy": месяц 1..12, день в пределах месяца (с учетом високосных лет)
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

// "hh:mm:ss": часы 0..23, минуты и секунды 0..59
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

// Разбор строки "dd.mm.yyyy hh:mm:ss hh:mm:ss Message"
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
// Формирование дейтаграмм
// ---------------------------------------------------------------------------

static void append_u32(std::string& buf, unsigned int value)
{
    unsigned int n = htonl(value);
    buf.append((const char*)&n, 4);
}

// Сообщение, подготовленное к отправке
struct Datagram
{
    std::string data;       // содержимое дейтаграммы
    bool        is_stop;    // текст сообщения - "stop"
    bool        confirmed;  // сервер подтвердил получение
};

// Загрузка сообщений из файла. Номер сообщения = позиция в векторе.
static bool load_messages(const char* filename, std::vector<Datagram>& out)
{
    std::ifstream file(filename, std::ios::in | std::ios::binary);
    std::string line;
    unsigned int line_no = 0, bad_lines = 0;

    if (!file.is_open())
        return false;

    while (std::getline(file, line))
    {
        Message msg;
        Datagram dg;
        unsigned int idx = (unsigned int)out.size();

        line_no++;

        if (!line.empty() && line[line.size() - 1] == '\r')
            line.erase(line.size() - 1);

        if (line.empty())
            continue;

        if (!parse_line(line, &msg))
        {
            bad_lines++;
            if (bad_lines <= MAX_REPORTED_BAD)
                printf("Line %u: invalid format, skipped\n", line_no);
            continue;
        }

        append_u32(dg.data, idx);
        append_u32(dg.data, msg.date);
        append_u32(dg.data, msg.time1);
        append_u32(dg.data, msg.time2);
        append_u32(dg.data, (unsigned int)msg.text.size());
        dg.data.append(msg.text);

        if (dg.data.size() > MAX_UDP_PAYLOAD)
        {
            printf("Line %u: message is too long for a UDP datagram, skipped\n", line_no);
            continue;
        }

        dg.is_stop = (msg.text == "stop");
        dg.confirmed = false;
        out.push_back(dg);
    }

    if (bad_lines > MAX_REPORTED_BAD)
        printf("... %u invalid lines skipped in total\n", bad_lines);

    return true;
}

// ---------------------------------------------------------------------------
// Работа с сокетом
// ---------------------------------------------------------------------------

static int init()
{
#ifdef _WIN32
    WSADATA wsa_data;
    return (0 == WSAStartup(MAKEWORD(2, 2), &wsa_data));
#else
    return 1;
#endif
}

static void deinit()
{
#ifdef _WIN32
    WSACleanup();
#endif
}

static int sock_err()
{
#ifdef _WIN32
    return WSAGetLastError();
#else
    return errno;
#endif
}

// Разбор аргумента "IP:PORT"
static bool parse_address(const char* arg, struct sockaddr_in* addr)
{
    std::string s(arg);
    size_t colon = s.rfind(':');
    std::string host;
    int port;

    if (colon == std::string::npos || colon == 0 || colon + 1 >= s.size())
        return false;

    host = s.substr(0, colon);
    port = atoi(s.c_str() + colon + 1);
    if (port <= 0 || port > 65535)
        return false;

    memset(addr, 0, sizeof(*addr));
    addr->sin_family = AF_INET;
    addr->sin_port = htons((unsigned short)port);
    addr->sin_addr.s_addr = inet_addr(host.c_str());

    if (addr->sin_addr.s_addr == INADDR_NONE)
    {
        // Не числовой адрес - определение адреса по имени узла
        struct hostent* he = gethostbyname(host.c_str());
        if (!he || he->h_addrtype != AF_INET)
            return false;
        memcpy(&addr->sin_addr, he->h_addr_list[0], 4);
    }

    return true;
}

// Ожидание дейтаграммы во входном буфере сокета не дольше ms миллисекунд.
// Возвращает > 0, если данные есть; 0 - таймаут; < 0 - ошибка.
static int wait_readable(SOCKET s, int ms)
{
    fd_set rfd;
    struct timeval tv;

    FD_ZERO(&rfd);
    FD_SET(s, &rfd);
    tv.tv_sec = ms / 1000;
    tv.tv_usec = (ms % 1000) * 1000;

    return select((int)s + 1, &rfd, 0, 0, &tv);
}

// ---------------------------------------------------------------------------

int main(int argc, char* argv[])
{
    std::vector<Datagram> msgs;
    std::vector<unsigned int> round;      // номера сообщений, отправленных в текущем проходе
    struct sockaddr_in server;
    static char buf[RECV_BUF_SIZE];
    unsigned int total, required, confirmed = 0, i;
    unsigned int datagrams_sent = 0, rounds = 0;
    int silent_rounds = 0;
    bool stop_confirmed = false;
    SOCKET s;

    // stdout без буферизации: сообщения не теряются при аварийной остановке
    setvbuf(stdout, 0, _IONBF, 0);

    if (argc < 3)
    {
        printf("Usage: %s IP:PORT FILE\n", argv[0]);
        return 1;
    }

    if (!init())
    {
        printf("Error: socket library initialization failed\n");
        return 1;
    }

    if (!parse_address(argv[1], &server))
    {
        printf("Invalid server address: %s\n", argv[1]);
        deinit();
        return 1;
    }

    if (!load_messages(argv[2], msgs))
    {
        printf("Cannot open file: %s\n", argv[2]);
        deinit();
        return 1;
    }

    total = (unsigned int)msgs.size();
    required = (total < REQUIRED_MESSAGES) ? total : REQUIRED_MESSAGES;
    printf("Server: %s, messages in file: %u, required to deliver: %u\n", argv[1], total, required);

    if (total == 0)
    {
        deinit();
        return 0;
    }

    // Создание единственного UDP-сокета (блокирующий режим - по умолчанию)
    s = socket(AF_INET, SOCK_DGRAM, 0);
    if (s == INVALID_SOCKET)
    {
        printf("socket() error: %d\n", sock_err());
        deinit();
        return 1;
    }

#ifdef _WIN32
    {
        BOOL report = FALSE;
        DWORD bytes = 0;
        WSAIoctl(s, SIO_UDP_CONNRESET, &report, sizeof(report), 0, 0, &bytes, 0, 0);
    }
#endif

    while (confirmed < required && !stop_confirmed)
    {
        unsigned int need = required - confirmed;
        bool received_any = false;

        // 1. Выбор сообщений для отправки: неподтвержденные сообщения (кроме
        //    "stop") по порядку, но не больше, чем осталось загрузить.
        round.clear();
        for (i = 0; i < total && round.size() < need; i++)
            if (!msgs[i].confirmed && !msgs[i].is_stop)
                round.push_back(i);

        // Все остальные нужные сообщения подтверждены - очередь сообщения "stop"
        if (round.empty())
        {
            for (i = 0; i < total; i++)
                if (!msgs[i].confirmed && msgs[i].is_stop)
                {
                    round.push_back(i);
                    break;
                }
        }

        // 2. Отправка выбранных дейтаграмм
        rounds++;
        for (i = 0; i < round.size(); i++)
        {
            const std::string& d = msgs[round[i]].data;
            int r = sendto(s, d.data(), (int)d.size(), 0, (struct sockaddr*)&server, sizeof(server));
            if (r < 0)
                printf("sendto() error: %d\n", sock_err());
            else
                datagrams_sent++;
        }

        // 3. Прием подтверждений. Ожидание каждой следующей дейтаграммы - до 100 мс.
        for (;;)
        {
            struct sockaddr_in from;
            socklen_t fromlen = sizeof(from);
            int r, k;
            bool round_done;

            if (wait_readable(s, WAIT_MS) <= 0)
                break; // 100 мс без дейтаграмм - повторная отправка

            r = recvfrom(s, buf, sizeof(buf), 0, (struct sockaddr*)&from, &fromlen);
            if (r < 0)
                continue; // например, ICMP "port unreachable" (сервер еще не запущен)

            received_any = true;

            // Извлечение номеров подтвержденных сообщений
            for (k = 0; k + 4 <= r; k += 4)
            {
                unsigned int n;
                memcpy(&n, buf + k, 4);
                n = ntohl(n);

                if (n < total && !msgs[n].confirmed)
                {
                    msgs[n].confirmed = true;
                    confirmed++;
                    if (msgs[n].is_stop)
                        stop_confirmed = true;
                }
            }

            if (confirmed >= required || stop_confirmed)
                break;

            // Все сообщения текущего прохода подтверждены - можно сразу
            // отправлять следующие (не дожидаясь таймаута)
            round_done = true;
            for (i = 0; i < round.size(); i++)
                if (!msgs[round[i]].confirmed)
                {
                    round_done = false;
                    break;
                }
            if (round_done)
                break;
        }

        if (received_any)
            silent_rounds = 0;
        else if (++silent_rounds >= MAX_SILENT_ROUNDS)
        {
            printf("Error: no response from server (%d retries)\n", silent_rounds);
            break;
        }
    }

    closesocket(s);
    deinit();

    printf("Confirmed by server: %u of %u required (datagrams sent: %u, rounds: %u)\n",
        confirmed, required, datagrams_sent, rounds);

    if (stop_confirmed && confirmed < required)
        printf("Warning: 'stop' message delivered, server terminated\n");

    return (confirmed >= required) ? 0 : 1;
}
