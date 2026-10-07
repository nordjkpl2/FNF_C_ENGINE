#ifndef VECTOR_H
#define VECTOR_H

typedef struct {
    int length;
    int capacity;
    void** contents; 
} Vector;

Vector* Vector_New(int capacity);
void Vector_NewFromAddress(Vector*, int capacity);
void Vector_Empty(Vector* vec);
void Vector_Push(Vector*, void*);
void* Vector_Pop(Vector*);
void Vector_Free(Vector*);
#endif