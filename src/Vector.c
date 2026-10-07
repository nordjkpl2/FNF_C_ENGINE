#include"std.h"
#include"Vector.h"

Vector* Vector_New(int startCapacity) {
    Vector* vec = malloc(sizeof(Vector));

    if(startCapacity < 10)
        startCapacity = 10;

    vec->length = 0;
    vec->capacity = startCapacity;
    vec->contents = malloc(sizeof(void*) * vec->capacity);

    return vec;
}

void Vector_Push(Vector* vec, void* p) {
    if(vec->length >= vec->capacity) {
        vec->capacity += 10;
        vec->contents = realloc(vec->contents, sizeof(void*) * vec->capacity);
    }

    vec->contents[vec->length++] = p;
}

void* Vector_Pop(Vector* vec) {
    if(vec->length == 0) 
        return NULL;

    return vec->contents[--vec->length];
}

void Vector_NewFromAddress(Vector* vec, int startCapacity) { 
    vec->length = 0;

    if(startCapacity < 10)
        startCapacity = 10;

    vec->capacity = startCapacity;
    vec->contents = malloc(sizeof(void*) * vec->capacity);
}

void Vector_Empty(Vector* vec) {
    for(int i = 0; i < vec->length; i++) {
        void** ptr = vec->contents + i;
        *ptr = NULL;
    }
    vec->length = 0;
}

void Vector_Free(Vector* vec) {
    free(vec->contents);
    free(vec);
}