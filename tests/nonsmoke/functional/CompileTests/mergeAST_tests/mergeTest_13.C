typedef int Integer;
int a;

// These are the same and yet violate ODR
void foo( int b );
void foo( int c );
void foo( int d ) {}

class X 
   {
     public:
          int e;
   };
