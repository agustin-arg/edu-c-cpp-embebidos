# Instalar y configurar copilador de C/C++ para windows

---

**Fuentes**:

- [Instalación de MinGW para compilar en C/C++ 👨🏻‍💻 - YouTube](https://www.youtube.com/watch?v=GEZLjRSY_m8)
- [C/C++ con VSCode 💻 FÁCIL y RÁPIDO - YouTube](https://www.youtube.com/watch?v=K8ATqLVFfpo)

---

Para poder hacer las practicas en código, instalé el copilador desde:

[MinGW - Minimalist GNU for Windows download | SourceForge.net](https://sourceforge.net/projects/mingw/)

Al ejecutar el gestor de intalación seleccioné mingw32-base ymingw32-gcc-g++. Agregé el path del copilador a las variables de entorno y reinicie la termina. Para comprobar que el copilador funciona:

```plaintext
gcc --version

```

Aunque reicié la terminal, el comando no me funcionaba, reinicié el OS y el comando ya funcionó.

Cree un archivo con el código de ejemplo que se encuentra en el libro:

```C
/* Figura 2.1: fig02_01.c
Un primer programa en C */
#include <stdio.h>

/* la función main inicia la ejecución del programa */
int main( void )
{
  printf( "Bienvenido a C!\n" );

  return 0; /* indica que el programa terminó con éxito */

} /* fin de la función main */
```

Copilé con:

```plaintext
gcc test.c
```

y Ejecuté con:

```plaintext
./a.exe
```
