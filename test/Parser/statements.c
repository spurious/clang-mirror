// RUN: clang -fsyntax-only %s

int test1() {
  { ; {  ;;}} ;;
}

int test2() {
  if (0) { if (1) {} } else { }

  do { } while (0); 
  
  while (0) while(0) do ; while(0);

  for (0;0;0)
    for (;;)
      for (9;0;2)
        ;
  for (int X = 0; 0; 0);
}

int test3() {
    switch (0) {
    
    case 4:
      if (0) {
    case 6: ;
      }
    default:
      ;     
  }
}

int test4() {
  if (0);
  
  int X;  // declaration in a block.
  
  if (0);
}
