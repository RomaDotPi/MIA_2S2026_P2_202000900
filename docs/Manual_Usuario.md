# Manual de usuario — ExtreamFS

**Manejo e Implementación de Archivos — Proyecto 2 — 2S2026**  
**Rodrigo Mérida — Carné 202000900 — Sección D**  
**Auxiliar:** Natalia Calderón  

## 1. Iniciar la aplicación

Requisitos locales: GNU/Linux, CMake, compilador C++17, Node.js, npm y Graphviz.

Desde la carpeta del proyecto abra dos terminales:

```bash
./run_backend.sh
```

```bash
./run_frontend.sh
```

Abra `http://localhost:5173`. El indicador superior confirma si la API está conectada.

## 2. Conocer la interfaz

- **Terminal:** escriba o cargue un archivo `.smia`, ejecute comandos y revise mensajes.
- **Discos:** consulte discos, particiones y montajes sin escribir comandos.
- **Explorador:** abra carpetas y previsualice archivos de la sesión activa.
- **Journal:** consulte las operaciones guardadas en una partición EXT3.
- **Sesión:** ingrese ID, usuario y contraseña; también puede cerrar sesión.

Los datos gráficos se actualizan después de cada comando. El borrado de una partición abre una confirmación para evitar eliminaciones accidentales.

## 3. Flujo recomendado

1. Cree un disco con `mkdisk`.
2. Cree una partición con `fdisk`.
3. Móntela con `mount` y anote su ID.
4. Formatee con `mkfs -fs=3fs` para usar journal y `loss`.
5. Inicie sesión con `root`, contraseña `123`.
6. Administre usuarios, carpetas y archivos.
7. Use Explorador, Journal y reportes para comprobar el resultado.

Para el carné 202000900, el primer montaje normalmente genera `001A`. Confirme siempre con `mounted`.

## 4. Ejemplo completo

```text
mkdisk -size=10 -unit=M -fit=FF -path=/tmp/disco1.mia
fdisk -size=7 -unit=M -type=P -fit=WF -path=/tmp/disco1.mia -name=Part1
mount -path=/tmp/disco1.mia -name=Part1
mounted
mkfs -type=full -fs=3fs -id=001A
login -user=root -pass=123 -id=001A
mkdir -p -path=/home/usac/mia
mkfile -r -path=/home/usac/mia/archivo.txt -size=900
cat -file1=/home/usac/mia/archivo.txt
journaling -id=001A
logout
```

## 5. Discos y particiones

```text
mkdisk -size=20 -unit=M -fit=BF -path=/tmp/proyecto.mia
fdisk -size=8 -unit=M -type=P -fit=WF -path=/tmp/proyecto.mia -name=Primaria1
fdisk -size=10 -unit=M -type=E -fit=FF -path=/tmp/proyecto.mia -name=Extendida
fdisk -size=4 -unit=M -type=L -fit=BF -path=/tmp/proyecto.mia -name=Logica1
fdisk -add=1 -unit=M -path=/tmp/proyecto.mia -name=Primaria1
mount -path=/tmp/proyecto.mia -name=Primaria1
unmount -id=001A
```

Para eliminar:

```text
fdisk -delete=fast -path=/tmp/proyecto.mia -name=Primaria1
```

La interfaz pedirá confirmación. En un script, repita el comando agregando `-confirm`. `fast` libera el descriptor; `full` también limpia con ceros el espacio de la partición.

## 6. Formato, journal y pérdida

```text
mkfs -type=full -fs=2fs -id=001A
mkfs -type=full -fs=3fs -id=001A
journaling -id=001A
loss -id=001A
```

EXT2 no tiene journal. EXT3 conserva las 50 operaciones más recientes. `loss` solamente acepta EXT3: muestra bitmaps antes/después, limpia estructuras de datos y conserva superbloque y journal. Después de ejecutarlo, cierre la sesión desde la interfaz como indica la prueba oficial. Es destructivo para el contenido del sistema simulado.

## 7. Usuarios y grupos

Estos comandos administrativos requieren la sesión `root`:

```text
mkgrp -name=devs
mkusr -user=ana -pass=clave -grp=devs
chgrp -user=ana -grp=root
rmusr -user=ana
rmgrp -name=devs
```

La contraseña no se muestra en la salida ni en el journal. Usuario, contraseña y grupo admiten hasta 10 caracteres.

## 8. Archivos, carpetas y permisos

```text
mkdir -p -path=/home/docs
mkfile -r -path=/home/docs/a.txt -size=128
cat -file1=/home/docs/a.txt -file2=/users.txt
rename -path=/home/docs/a.txt -name=b.txt
copy -path=/home/docs/b.txt -destino=/home
move -path=/home/b.txt -destino=/home/docs
find -path=/home -name=*.txt
chown -path=/home/docs -user=ana -r
chmod -path=/home/docs -ugo=750 -r
remove -path=/home/docs/b.txt
```

`-r` crea padres en `mkfile` y aplica cambios recursivos en `chown`/`chmod`. No se sobrescriben destinos existentes. `/`, `/users.txt` y movimientos hacia un descendiente propio están protegidos. Los permisos siguen el formato propietario/grupo/otros; por ejemplo, `750` significa `rwxr-x---`.

## 9. Reportes

```text
rep -id=001A -path=/tmp/reportes/mbr.jpg -name=mbr
rep -id=001A -path=/tmp/reportes/tree.svg -name=tree
rep -id=001A -path=/tmp/reportes/file.txt -path_file_ls=/home/docs/a.txt -name=file
rep -id=001A -path=/tmp/reportes/ls.jpg -path_file_ls=/home/docs -name=ls
```

Nombres disponibles: `mbr`, `disk`, `inode`, `block`, `bm_inode`, `bm_block`, `tree`, `sb`, `file` y `ls`. La carpeta de salida se crea automáticamente. Graphviz debe estar instalado para imágenes.

## 10. Scripts de calificación

1. Cargue la primera parte `.smia` en Terminal y ejecútela.
2. Consulte el ID real con `mounted`.
3. Reemplace marcadores como `XX1A` por ese ID en las partes siguientes.
4. Ejecute las partes en orden y sin reiniciar el backend.
5. Si un comando usa `-cont`, asegúrese de que la ruta exista en el servidor EC2, no solamente en su computadora.

## 11. Mensajes frecuentes

| Mensaje o síntoma | Causa habitual | Solución |
|---|---|---|
| API desconectada | Backend detenido o URL incorrecta | Inicie backend y revise `VITE_API_BASE_URL` |
| `id no montado` | ID equivocado o backend reiniciado | Ejecute `mount` y `mounted` |
| `requiere sesión` | No hay usuario activo | Use el panel de sesión o `login` |
| `sin permiso` | UGO no permite la acción | Ingrese como propietario/root o ajuste `chmod` |
| `partición sin formato` | Falta `mkfs` | Formatee el montaje correcto |
| `solo disponible para EXT3` | Se usó journal/loss en EXT2 | Formatee con `-fs=3fs` |
| `Graphviz falló` | Falta `dot` | Instale Graphviz |
| La web carga pero no ejecuta | EC2:8080 no es accesible o hay contenido mixto | Revise grupo de seguridad y use HTTP con S3 website |

## 12. Cerrar y conservar datos

Detenga cada proceso con `Ctrl+C`. Montajes y sesión se pierden al reiniciar el backend, pero los archivos `.mia` persisten. Vuelva a montar la partición para continuar. Antes de borrar un disco o ejecutar `loss`, conserve una copia si necesita recuperar su contenido.
