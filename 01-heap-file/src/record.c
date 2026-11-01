#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <record.h>

const char* names[] = {
    "Alexandros",
    "Sofia",
    "Dimitris",
    "Anna",
    "Konstantinos",
    "Maria",
    "Georgios",
    "Eleni",
    "Petros",
    "Evangelia"
};


const char* surnames[] = {
    "Papadopoulos",
    "Georgiou",
    "Dimitriou",
    "Anagnostopoulos",
    "Karagiannis",
    "Mavromatis",
    "Nikolaou",
    "Christodoulou",
    "Kostopoulos",
    "Stamatopoulos"
};

const char* cities[] = {
    "Athina",
    "Patra",
    "Irakleio",
    "Larisa",
    "Volos",
    "Ioannina",
    "Chania",
    "Kalamata",
    "Rodos"
};


static int id = 0;

Record randomRecord(){
    Record record;
    // create a record
    record.id = id++;
    int r = rand() % (sizeof(names) / sizeof(names[0]));
    memcpy(record.name, names[r], strlen(names[r]) + 1);
    //
    r = rand() %  (sizeof(surnames) / sizeof(surnames[0]));
    memcpy(record.surname, surnames[r], strlen(surnames[r]) + 1);
    //
    r = rand() %  (sizeof(cities) / sizeof(cities[0]));
    memcpy(record.city, cities[r], strlen(cities[r]) + 1);
    return record;
}

void printRecord(Record record){
    printf("(%d,%s,%s,%s)\n",record.id,record.name,record.surname,record.city);

}



