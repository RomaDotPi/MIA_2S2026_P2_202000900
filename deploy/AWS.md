# Despliegue en AWS

Esta guía deja el frontend React en un sitio web estático de S3 y el backend C++ en EC2. Complete estos pasos cuando pueda iniciar sesión en AWS.

## 1. Crear EC2

1. Cree una instancia Ubuntu de tamaño elegible para capa gratuita, si su cuenta la ofrece.
2. Descargue y guarde la llave `.pem`; no la copie dentro del proyecto.
3. En el grupo de seguridad permita SSH (22) solamente desde su IP.
4. Permita TCP 8080 para la demostración. Al terminar la evaluación, quite esta regla y detenga o elimine la instancia.
5. Conéctese desde la consola con EC2 Instance Connect o por SSH.

## 2. Preparar el backend

En Ubuntu:

```bash
sudo apt update
sudo apt install -y build-essential cmake graphviz git
git clone URL_DEL_REPOSITORIO ExtreamFS
cd ExtreamFS
cmake -S backend -B backend/build -DCMAKE_BUILD_TYPE=Release
cmake --build backend/build -j2
ctest --test-dir backend/build --output-on-failure
./backend/build/extreamfs 8080
```

Compruebe en otra terminal:

```bash
curl http://localhost:8080/health
```

Para dejarlo como servicio, copie `deploy/extreamfs.service.example`, sustituya `USUARIO_EC2` y `RUTA_PROYECTO`, e instálelo:

```bash
sudo cp deploy/extreamfs.service.example /etc/systemd/system/extreamfs.service
sudo systemctl daemon-reload
sudo systemctl enable --now extreamfs
sudo systemctl status extreamfs
```

## 3. Compilar el frontend

En su equipo local cree `frontend/.env`:

```text
VITE_API_BASE_URL=http://IP_PUBLICA_EC2:8080
```

Después:

```bash
cd frontend
npm ci
npm run build
```

La carpeta que se publica es `frontend/dist`.

## 4. Publicar en S3

1. Cree un bucket de propósito general con nombre globalmente único.
2. En Propiedades, habilite alojamiento de sitio web estático.
3. Use `index.html` como documento de índice y error.
4. Para la entrega académica pública, ajuste Bloquear acceso público y agregue una política de lectura limitada a `arn:aws:s3:::NOMBRE_BUCKET/*`. No guarde discos `.mia`, llaves o credenciales en el bucket.
5. Suba el contenido de `frontend/dist`, no la carpeta contenedora.
6. Abra el endpoint de sitio web mostrado por S3 y pruebe login, Explorador y Journal.

S3 website usa HTTP. Si se agrega CloudFront/HTTPS, el backend también debe publicarse con HTTPS para que el navegador no bloquee las solicitudes por contenido mixto.

## 5. Lista de comprobación

- API responde `{"ok":true}`.
- El frontend muestra “API conectada”.
- Se puede crear, montar y formatear un disco en una ruta escribible de EC2.
- Login `root / 123` funciona.
- Explorador y Journal cargan.
- Grupo de seguridad no deja SSH abierto a todo Internet.
- La llave `.pem`, `.env`, discos y credenciales no están en Git/S3.
- Después de la calificación se detienen/eliminan EC2 y S3 para evitar cargos.

Referencias oficiales:

- [Conexión a Linux con EC2 Instance Connect](https://docs.aws.amazon.com/AWSEC2/latest/UserGuide/ec2-instance-connect-methods.html)
- [Alojamiento de un sitio web estático en S3](https://docs.aws.amazon.com/AmazonS3/latest/userguide/WebsiteHosting.html)
- [Tutorial de configuración del sitio en S3](https://docs.aws.amazon.com/AmazonS3/latest/userguide/HostingWebsiteOnS3Setup.html)
