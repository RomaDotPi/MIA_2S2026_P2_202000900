# ExtreamFS — MIA Proyecto 2, 2S2026

Aplicación web para simular sistemas de archivos EXT2 y EXT3 sobre discos binarios `.mia`.

## Datos académicos

- Estudiante: Rodrigo Mérida
- Carné: 202000900
- Sección: D
- Auxiliar: Natalia Calderón

## Tecnologías

- Backend: C++17 y API HTTP REST con sockets POSIX
- Frontend: React 18 y Vite
- Reportes: Graphviz
- Plataforma: GNU/Linux; despliegue previsto en EC2 y S3

## Inicio local

En dos terminales, desde la raíz del proyecto:

```bash
./run_backend.sh
./run_frontend.sh
```

Abra `http://localhost:5173`. La API queda disponible en `http://localhost:8080`.

Para cambiar la API del frontend:

```bash
cp frontend/.env.example frontend/.env
```

Edite `VITE_API_BASE_URL` antes de ejecutar `npm run build`.

## Verificación

```bash
cmake -S backend -B backend/build
cmake --build backend/build --clean-first -j2
ctest --test-dir backend/build --output-on-failure
cd frontend && npm ci && npm run build
```

## Funciones entregadas

- Administración de discos y particiones primarias, extendidas y lógicas.
- `fdisk -add`, `fdisk -delete=fast|full`, confirmación gráfica y `unmount`.
- Formato EXT2 y EXT3; EXT3 incluye journal circular de 50 entradas.
- Usuarios, grupos, sesiones y permisos UGO.
- Archivos y carpetas: crear, leer, borrar, renombrar, copiar, mover, buscar, cambiar propietario y permisos.
- `loss` para simular pérdida de EXT3 conservando superbloque y journal.
- Reportes Graphviz y de texto.
- Interfaz con Terminal, Discos, Explorador y Journal.
- API de estado, exploración y bitácora.

La sintaxis completa está en `docs/Manual_Usuario.md`. La arquitectura, estructuras y API están en `docs/Manual_Tecnico.md`; el despliegue está en `deploy/AWS.md`.

## Seguridad

Las contraseñas se reemplazan por `***` al mostrar comandos y al escribir el journal. No se debe subir a Git ninguna llave `.pem`, credencial de AWS, disco de prueba ni archivo `.env` privado.
