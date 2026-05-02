#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <locale.h>
#include <Windows.h>

typedef enum add {
    scholarship = 1,
    session,
    form
} add;

typedef union student_information {
    int scholarship;
    float session;
    char form[8];
} sti;

typedef struct Students {
    char FirstName[50];
    char LastName[50];
    char Patronymic[50];
    char Group[14];
    int Age;
    float AverageGrade;
    add information;
    sti info;
} sts;

typedef struct Node {
    sts data;
    struct Node* prev;
    struct Node* next;
} Node;

typedef struct List {
    Node* head;
    Node* tail;
    int size;
} List;

List* create_list() {
    List* lst = (List*)malloc(sizeof(List));
    lst->head = lst->tail = NULL;
    lst->size = 0;
    return lst;
}

Node* create_node(sts* data) {
    Node* node = (Node*)malloc(sizeof(Node));
    node->data = *data;
    node->next = node->prev = NULL;
    return node;
}

void add_node(List* lst, sts* data) {
    Node* node = create_node(data);
    if (lst->head == NULL) {
        lst->head = lst->tail = node;
    }
    else {
        lst->tail->next = node;
        node->prev = lst->tail;
        lst->tail = node;
    }
    lst->size++;
}

void delete_node(List* lst, int index) {
    if (index < 1 || index > lst->size) {
        printf("Неверный номер студента %d\n", index);
        return;
    }

    Node* current = lst->head;
    for (int i = 1; i < index; i++)
        current = current->next;

    if (current->prev)
        current->prev->next = current->next;
    else
        lst->head = current->next;

    if (current->next)
        current->next->prev = current->prev;
    else
        lst->tail = current->prev;

    free(current);
    lst->size--;
}

void free_list(List* lst) {
    Node* current = lst->head;
    while (current) {
        Node* tmp = current;
        current = current->next;
        free(tmp);
    }
    free(lst);
}

void addition(sts* data) {
    printf("1) Стипендия 2) Средняя оценка за сессию 3) Форма обучения\n");
    printf("Выберите, что вы хотите добавить: ");
    int a;
    scanf("%d", &a);
    memset(&data->info, 0, sizeof(data->info));

    while (1) {
        switch (a) {
        case scholarship:
            printf("Введите размер стипендии: ");
            scanf("%d", &data->info.scholarship);
            data->information = scholarship;
            return;
        case session:
            printf("Введите среднюю оценку за сессию: ");
            scanf("%f", &data->info.session);
            data->information = session;
            return;
        case form:
            printf("Введите форму обучения: ");
            scanf("%7s", data->info.form);
            data->information = form;
            return;
        default:
            printf("Неверный ввод, выберите 1-3: ");
            scanf("%d", &a);
        }
    }
}

void input_student(sts* data) {
    printf("Введите информацию о студенте:\n");
    printf("Фамилия: ");
    scanf("%49s", data->LastName);
    printf("Имя: ");
    scanf("%49s", data->FirstName);
    printf("Отчество: ");
    scanf("%49s", data->Patronymic);
    printf("Группа: ");
    scanf("%13s", data->Group);
    printf("Возраст: ");
    scanf("%d", &data->Age);
    printf("Средний балл: ");
    scanf("%f", &data->AverageGrade);
    addition(data);
}

void print_student(sts* student, int index) {
    printf("%d. %-13s %-15s %-15s %-13s %-7d %-13.2f",
        index,
        student->LastName,
        student->FirstName,
        student->Patronymic,
        student->Group,
        student->Age,
        student->AverageGrade);

    switch (student->information) {
    case scholarship:
        printf("%d\n", student->info.scholarship);
        break;
    case session:
        printf("%.2f\n", student->info.session);
        break;
    case form:
        printf("%s\n", student->info.form);
        break;
    }
}

void print_list(List* lst) {
    if (lst->size == 0) {
        printf("Список пуст!\n");
        return;
    }

    printf("\n%-15s %-15s %-15s %-13s %s %s %s\n",
        "Фамилия", "Имя", "Отчество", "Группа", "Возраст", "Средний балл", "Доп. инфо");
    printf("--------------------------------------------------------------------------------------------------\n");

    Node* current = lst->head;
    int i = 1;
    while (current) {
        print_student(&current->data, i++);
        current = current->next;
    }

    printf("--------------------------------------------------------------------------------------------------\n");
    printf("Всего студентов: %d\n", lst->size);
}

void save_to_file(List* lst) {
    FILE* file = fopen("data.txt", "w");
    if (!file) {
        printf("Ошибка открытия файла для сохранения\n");
        return;
    }

    fprintf(file, "%-15s %-15s %-15s %-13s %s %s %s\n",
        "Фамилия", "Имя", "Отчество", "Группа", "Возраст", "Средний балл", "Доп. инфо");
    fprintf(file, "--------------------------------------------------------------------------------------------------\n");

    Node* current = lst->head;
    int i = 1;
    while (current) {
        fprintf(file, "%d. %-13s %-15s %-15s %-13s %-7d %-13.2f",
            i++,
            current->data.LastName,
            current->data.FirstName,
            current->data.Patronymic,
            current->data.Group,
            current->data.Age,
            current->data.AverageGrade);

        switch (current->data.information) {
        case scholarship:
            fprintf(file, "%d\n", current->data.info.scholarship);
            break;
        case session:
            fprintf(file, "%.2f\n", current->data.info.session);
            break;
        case form:
            fprintf(file, "%s\n", current->data.info.form);
            break;
        }
        current = current->next;
    }
    fprintf(file, "--------------------------------------------------------------------------------------------------\n");
    fprintf(file, "Всего студентов: %d\n", lst->size);
    fclose(file);
    printf("Данные сохранены.\n");
}

void load_from_file(List* lst) {
    FILE* file = fopen("data.txt", "r");
    if (!file) {
        printf("Ошибка открытия файла для загрузки\n");
        return;
    }

    Node* tmp = lst->head;
    while (tmp) {
        Node* next = tmp->next;
        free(tmp);
        tmp = next;
    }
    lst->head = lst->tail = NULL;
    lst->size = 0;

    char buff[256];
    fgets(buff, sizeof(buff), file);
    fgets(buff, sizeof(buff), file);

    while (fgets(buff, sizeof(buff), file)) {
        if (buff[0] == '-' || strstr(buff, "Всего студентов") != NULL) continue;

        sts s;
        int num;
        char additional[50];
        if (sscanf(buff, "%d. %49s %49s %49s %13s %d %f %s",
            &num,
            s.LastName, s.FirstName, s.Patronymic, s.Group,
            &s.Age, &s.AverageGrade, additional) < 7)
            continue;

        if (isalpha(additional[0])) {
            strcpy(s.info.form, additional);
            s.information = form;
        }
        else if (strchr(additional, '.')) {
            s.info.session = atof(additional);
            s.information = session;
        }
        else {
            s.info.scholarship = atoi(additional);
            s.information = scholarship;
        }

        add_node(lst, &s);
    }

    fclose(file);
    printf("Данные загружены.\n");
}

void find_student(List* lst) {
    if (lst->size == 0) {
        printf("Список пуст!\n");
        return;
    }

    char lastname[50];
    printf("Введите фамилию: ");
    scanf("%49s", lastname);

    Node* current = lst->head;
    int found = 0;
    int i = 1;

    while (current) {
        if (strcmp(current->data.LastName, lastname) == 0) {
            print_student(&current->data, i);
            found = 1;
        }
        current = current->next;
        i++;
    }

    if (!found) printf("Студент не найден.\n");
}

void menu(List* lst) {
    sts temp;
    int choice, index;

    while (1) {
        printf("\n1) Загрузить базу\n2) Добавить студента\n3) Сохранить\n4) Найти\n5) Удалить\n6) Показать всех\n7) Выйти\nВаш выбор: ");
        scanf("%d", &choice);
        switch (choice) {
        case 1:
            load_from_file(lst);
            break;
        case 2:
            input_student(&temp);
            add_node(lst, &temp);
            break;
        case 3:
            save_to_file(lst);
            break;
        case 4:
            find_student(lst);
            break;
        case 5:
            printf("Введите номер студента для удаления: ");
            scanf("%d", &index);
            delete_node(lst, index);
            break;
        case 6:
            print_list(lst);
            break;
        case 7:
            return;
        default:
            printf("Выберите пункт 1–7\n");
        }
    }
}

int main() {
    setlocale(LC_ALL, "Russian");
    SetConsoleCP(1251);
    SetConsoleOutputCP(1251);

    List* db = create_list();
    menu(db);
    free_list(db);

    return 0;
}
