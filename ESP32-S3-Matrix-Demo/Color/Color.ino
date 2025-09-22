// Color.ino
#include "WS_Matrix.h"
// English: Please note that the brightness of the lamp bead should not be too high, which can easily cause the temperature of the board to rise rapidly, thus damaging the board !!!
void setup()
{
    Matrix_Init();
}
int x=0;

void loop()
{
  RGB_Matrix1(x);
  delay(30);
  x++;
  if(x==24)
    x=0;
  
}
