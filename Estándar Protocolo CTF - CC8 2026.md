# **Estándar Protocolo CTF - CC8 2026**

## **1. CONEXIÓN**

### **1.1 Transporte**

Define la tecnología encargada de mover los bytes entre las máquinas participantes y las garantías de entrega que esta ofrece. Todos los proyectos de la clase deben acordar el mismo transporte para que sus conexiones sean compatibles entre sí.  
Se establece un esquema híbrido:

> * **TCP:** Para toda la comunicación de la partida (unirse, moverse, tomar la bandera, estado del juego, fin de partida). Garantiza que los mensajes lleguen completos y en el orden en que se enviaron.  
> * **UDP:** Exclusivamente para el descubrimiento de servidores en la red local (desarrollado en la sección 1.3).

Requisitos para cada proyecto:

> * Usar los sockets básicos que ya trae el lenguaje, sin librerías externas de conexión (por ejemplo: socket en Python, net en Go, TcpClient en C#, net en Node nunca ws-, POSIX/Winsock en C++, StreamPeerTCP en Godot, dart:io en Dart).  
> * Usar los sockets básicos que ya trae el lenguaje, sin librerías externas de conexión (por ejemplo: socket en Python, net en Go, TcpClient en C#, net en Node —nunca ws—, POSIX/Winsock en C++, StreamPeerTCP en Godot, dart:io en Dart).  
> * En modo cliente, abrir una única conexión TCP hacia el servidor y utilizarla para todo el intercambio de mensajes del juego.  
> * En modo servidor, aceptar y mantener conexiones TCP simultáneas de hasta 100 clientes.  
> * Mantener, además, un socket UDP independiente dedicado solo al descubrimiento.

### **1.2 Puertos**

Establece los números de puerto en los que los servidores escuchan, tanto para el descubrimiento como para la partida. Separar ambos puertos evita que la búsqueda de servidores interfiera con el tráfico del juego en curso.  
Se definen dos tipos de puerto:

> * **Puerto de descubrimiento (UDP):** Fijo e igual para todos los proyectos de la clase: **8888**. Todo servidor debe escuchar ahí sin excepción.  
> * **Puerto de juego (TCP):** Cada servidor lo elige libremente (por ejemplo, 8889) y no necesita coincidir entre proyectos.

Requisitos para cada proyecto:

> * El servidor debe anunciar su puerto TCP dentro del mensaje de respuesta al descubrimiento, ya que el cliente no lo conoce de antemano.  
> * El cliente primero pregunta por el puerto UDP fijo (8888), lee el puerto TCP que el servidor le indica, y con ese puerto abre la conexión de juego.  
> * Ningún proyecto debe hardcodear un puerto TCP ajeno: siempre se obtiene dinámicamente de la respuesta de descubrimiento.

### **1.3 Descubrimiento de servidores**

Describe el procedimiento mediante el cual un cliente localiza los servidores disponibles en la red local sin conocer sus direcciones IP de antemano. Este mecanismo depende del puerto UDP definido en la sección 1.2.  
El descubrimiento sigue dos vías:

> 1. **Automática (broadcast UDP):** El cliente envía una pregunta a toda la red y cada servidor activo responde de forma individual.  
   * El cliente no conoce la IP del servidor, por lo que envía un paquete de solicitud a la dirección de broadcast de la red local (usualmente 255.255.255.255 o el broadcast de la subred, como 192.168.1.255).  
   * Este paquete se envía a todos los dispositivos conectados a esa red local.  
   * El servidor examina la IP de origen del paquete de solicitud (la IP del cliente) para saber a dónde responder.  
   * El servidor envía un paquete de respuesta directamente a la IP del cliente.  
   * Al recibir la respuesta, el cliente comprueba la IP de origen del paquete (que es la IP real del servidor en la LAN) y lo muestra en la lista.  
> 2. **Manual (respaldo):** El cliente permite escribir directamente la IP de un servidor, para los casos en que el broadcast no funcione (router o WiFi que lo bloquee).

Requisitos para cada proyecto:

> * El cliente debe enviar por broadcast (255.255.255.255:8888) un mensaje del tipo:  
>   {"type": "discover", "v": 1}  
> * El servidor debe responder directamente al remitente con:  
>   {"type": "server_info", "v": 1, "name": "...", "tcp_port": 8889, "state": "lobby", "players": 3}  
> * El cliente debe mostrar la lista de servidores encontrados y permitir al usuario elegir uno.  
> * El cliente debe incluir, sin excepción, la opción de conexión manual por IP como respaldo al broadcast.

## 

## **2. EL IDIOMA DE LOS MENSAJES**

### **2.1 Delimitación de mensajes (framing)**

Especifica la regla que permite identificar dónde termina un mensaje y dónde comienza el siguiente. Este punto es indispensable porque TCP entrega los datos como un chorro continuo de bytes, sin ninguna separación natural entre mensajes: dos mensajes pueden llegar pegados en una sola lectura, o uno solo puede llegar partido en dos lecturas distintas.  
Se define la siguiente regla:

> * **Un mensaje = una línea:** Cada mensaje es un texto JSON completo, seguido del carácter de salto de línea (\n).  
> * El JSON de un mensaje no puede contener saltos de línea internos: debe escribirse siempre "aplastado" en una sola línea (sin indentación ni formato bonito).  
> * El salto de línea (\n) es exclusivamente el separador entre mensajes; nunca debe aparecer dentro del contenido de un campo.

Procedimiento obligatorio para leer mensajes:

> 1. Ir acumulando los bytes que llegan por el socket en un buffer (un cajón temporal).  
> 2. Cada vez que llegue un \n dentro de ese buffer, cortar ahí: todo lo acumulado ANTES del \n es un mensaje completo.  
> 3. Convertir ese texto cortado en un objeto JSON (parsear).  
> 4. Lo que quede DESPUÉS del \n se conserva en el buffer, porque puede ser el inicio del siguiente mensaje (o estar incompleto todavía).  
> 5. Repetir mientras la conexión siga abierta.

*Aclaración importante:* Esta regla aplica únicamente a mensajes enviados por TCP. Los mensajes de descubrimiento, enviados por UDP, se transmiten como paquetes completos en un solo paso y no requieren el carácter \n ni el proceso de buffer.

### **2.2 Formato y codificación**

Define el lenguaje en que se escriben los mensajes y la codificación de caracteres utilizada, de modo que todos los proyectos interpreten la misma información de forma idéntica, sin importar el lenguaje de programación en que estén escritos.

> * **Formato:** JSON. Todo mensaje del protocolo debe ser un objeto JSON válido, no un texto libre ni un formato inventado por cada proyecto.  
> * **Codificación:** UTF-8. Todo el texto (nombres de jugadores, motivos de error, etc.) debe codificarse y decodificarse en UTF-8 en ambos extremos de la conexión.  
> * **Campo identificador obligatorio:** Todo mensaje, sin excepción, debe incluir un campo llamado "type" como texto (string), cuyo valor identifica de qué mensaje se trata. Ningún mensaje puede omitirlo. Ejemplo: {"type": "join", ...}.  
> * **Valores numéricos:** Los campos de posición y dirección (x, y, dir) se representan como números, nunca como texto (ejemplo: "x": -1, no "x": "-1").  
> * **Estructura plana:** Los mensajes no deben anidar más de dos niveles de profundidad (por ejemplo, config dentro de welcome es aceptable; un tercer nivel dentro de config no lo es), para mantener el parseo simple en todos los lenguajes.

### **2.3 Catálogo de mensajes**

#### **Vista general del catálogo**

| Tipo | Dirección | Fase | Propósito | Campos principales   |
| :---- | :---- | :---- | :---- | :---- |
| discover | C → UDP | Cualquiera | Buscar servidores en la red | v |
| server_info | S → UDP | Cualquiera | Anunciar el servidor encontrado | v, name, tcp_port, state, players |
| join | C → S | Lobby | Unirse a la partida | v, name |
| input | C → S | Playing | Comunicar hacia dónde se mueve | dir (dir.x, dir.y) |
| interact | C → S | Playing | Tomar o robar la bandera | ninguno |
| welcome | S → C | Lobby | Asignar identidad y constantes | player_id, config |
| lobby | S → C | Lobby | Lista de jugadores en espera | players |
| countdown | S → C | Countdown | Mostrar la cuenta regresiva | seconds |
| start | S → C | Countdown-Playing | Iniciar la partida | ninguno |
| state | S → C | Playing | Replicar el mundo del juego | flag, players |
| game_over | S → C | Playing-Finished | Anunciar al ganador | winner |
| error | S → C | Cualquiera | Rechazar una acción inválida | reason |

#### 

#### **Detalle de campos por mensaje**

**1. Mensajes de descubrimiento (UDP):**

> * **discover** (Cliente → Broadcast UDP | Fase: cualquiera):  
  * v (entero): Versión del protocolo que habla el cliente.  
> * **server_info** (Servidor → Unicast UDP | Fase: cualquiera):  
  * v (entero): Versión del protocolo que habla el servidor.  
  * name (texto): Nombre del servidor.  
  * tcp_port (entero): Puerto TCP donde escucha el juego.  
  * state (texto): Fase actual ("lobby" / "playing").  
  * players (entero): Cantidad de jugadores conectados.

**2. Mensajes de cliente a servidor (TCP):**

> * **join** (Cliente → Servidor | Fase: lobby):  
  * v (entero): Versión del protocolo que habla el cliente.  
  * name (texto): Nombre del jugador, máximo 20 caracteres, UTF-8.  
> * **input** (Cliente → Servidor | Fase: playing):  
  * dir.x (entero): -1 = izquierda, 0 = quieto, 1 = derecha.  
  * dir.y (entero): -1 = arriba, 0 = quieto, 1 = abajo.  
> * **interact** (Cliente → Servidor | Fase: playing):  
  * *Sin campos:* Intenta tomar la bandera libre o robarla al portador.

**Combinaciones posibles de dir (x, y):**

> * (-1, -1): Arriba-Izquierda  
> * (0, -1): Arriba  
> * (1, -1): Arriba-Derecha  
> * (-1, 0): Izquierda  
> * (0, 0): Quieto  
> * (1, 0): Derecha  
> * (-1, 1): Abajo-Izquierda  
> * (0, 1): Abajo  
> * (1, 1): Abajo-Derecha

**3. Mensajes de servidor a cliente (TCP):**

> * **welcome** (Servidor → Cliente | Fase: lobby):  
  * player_id (texto): Identificador único que el servidor le asigna al jugador.  
  * config.map_size (número): Lado del mapa en unidades lógicas.  
  * config.circle_radius (número): Radio del círculo central.  
  * config.player_radius (número): Radio del cuerpo del jugador.  
  * config.interact_radius (número): Distancia máxima para tomar o robar la bandera.  
  * config.speed (número): Velocidad de movimiento en unidades por segundo.  
  * config.tick_rate (entero): Envíos de estado por segundo.  
> * **lobby** (Servidor → Cliente | Fase: lobby):  
  * players[].id (texto): Identificador de cada jugador conectado.  
  * players[].name (texto): Nombre visible de cada jugador conectado.  
> * **countdown** (Servidor → Cliente | Fase: countdown):  
  * seconds (entero): Segundos restantes para el inicio (5, 4, 3, 2, 1).  
> * **start** (Servidor → Cliente | Fase: countdown → playing):  
  * *Sin campos:* Marca el inicio exacto de la partida.  
> * **state** (Servidor → Cliente | Fase: playing):  
  * flag.owner (texto o nulo): ID del portador, o null si la bandera está libre.  
  * flag.x, flag.y (número): Posición actual de la bandera.  
  * players[].id (texto): Identificador del jugador.  
  * players[].x, players[].y (número): Posición actual de ese jugador (1 decimal).  
> * **game_over** (Servidor → Cliente | Fase: playing → finished):  
  * winner (texto): ID del jugador que ganó la partida.  
> * **error** (Servidor → Cliente | Fase: cualquiera):  
  * reason (texto): Motivo del rechazo (ej. "game_started", "server_full").

#### **Constantes y límites acordados**

| Categoría | Constante / Límite | Valor | Significado   |
| :---- | :---- | :---- | :---- |
| Constantes en welcome.config | map_size | 1000 | El mapa mide 1000 x 1000 unidades lógicas. |
|  | circle_radius | 300 | El círculo central mide 300 unidades de radio. |
|  | player_radius | 15 | El cuerpo del jugador mide 15 unidades de radio. |
|  | interact_radius | 40 | Hasta 40 unidades de distancia para tomar o robar la bandera. |
|  | speed | 200 | 200 unidades por segundo de velocidad de movimiento. |
|  | tick_rate | 20 | 20 envíos de estado por segundo. |
| Constantes del Servidor | countdown_seconds | 5 | La cuenta regresiva dura 5 segundos antes de iniciar. |
|  | discovery_port (UDP) | 8888 | Puerto fijo de descubrimiento, igual para toda la clase. |
| Límites del Protocolo | max_players | 100 | Máximo de jugadores por partida (según el enunciado). |
|  | name_max_length | 20 | Largo máximo del nombre del jugador en caracteres. |
|  | message_max_size | 64 KB | Tamaño máximo de un mensaje individual. |

## 

## 

## **3. REGLAS DE LA PARTIDA**

### **3.1 Secuencia de la partida**

> 1. Búsqueda de servidores disponibles (descubrimiento).  
> 2. Lobby / espera al comienzo de la partida.  
> 3. Countdown.  
> 4. Inicio de la partida:  
   * Ubicación inicial de los jugadores (spawn aleatorio, fuera del círculo).  
   * Movimiento libre de los jugadores.  
> 5. Eventos durante la partida (ocurren en cualquier momento y cualquier cantidad de veces):  
   * Captura de bandera.  
   * Robo de bandera.  
   * Salida del círculo con la bandera (condición de victoria).  
> 6. Fin de la partida / mostrar ganador.

### **3.2 Sistema de coordenadas**

> * Origen (0,0) ubicado en la esquina superior izquierda.  
> * El eje X crece hacia la derecha.  
> * El eje Y crece hacia abajo (Y negativo es arriba, Y positivo es abajo).

## 

## **4. AUTORIDAD Y SINCRONIZACIÓN**

### **4.1 Autoridad y validaciones**

| Acción | Qué envía el cliente | Qué valida el servidor | Resultado   |
| :---- | :---- | :---- | :---- |
| **Movimiento** | Dirección dir.x y dir.y | Que los valores sean -1, 0, 1; que la partida esté activa y el jugador conectado. | Calcula la nueva posición. |
| **Movimiento diagonal** | Dirección en ambos ejes | Normaliza la dirección para evitar mayor velocidad. | Mantiene la misma rapidez en todas las direcciones. |
| **Límites del mapa** | No envía información adicional | Comprueba que la posición permanezca entre 15 y 985 en ambos ejes. | Ajusta la posición si intenta salir del mapa. |
| **Captura de bandera** | Mensaje interact | Que la bandera esté libre y que la distancia sea menor o igual a 40. | Asigna la bandera al jugador. |
| **Robo de bandera** | Mensaje interact | Que otro jugador posea la bandera y que la distancia entre ambos sea menor o igual a 40. | Cambia el propietario de la bandera. |
| **Condición de victoria** | No envía un mensaje especial | Que el jugador tenga la bandera y haya salido completamente del círculo. | Finaliza la partida y declara al ganador. |
| **Mensaje recibido** | Mensaje JSON | Que tenga type, los campos requeridos y los tipos de datos correctos. | Procesa el mensaje o responde con un error. |
| **Fase de la partida** | Acción correspondiente | Que la acción esté permitida en la fase actual. | Acepta o rechaza la acción. |

### 

### **4.2 Sincronización del estado**

| Aspecto | Decisión   |
| :---- | :---- |
| **Autoridad principal** | El servidor |
| **Posiciones oficiales** | Las calcula el servidor |
| **Información enviada por el cliente** | Intenciones de movimiento e interacción |
| **Captura, robo y victoria** | Los valida el servidor |
| **Límites y velocidad** | Los controla el servidor |
| **Función del cliente** | Enviar acciones y mostrar el estado recibido |

## 

## 

## **5. MANEJO DE FALLAS Y DESCONEXIONES**

### **5.1 Manejo de errores**

Regla general: Cuando el servidor reciba un mensaje incorrecto, deberá:

> 1. Detectar el problema.  
> 2. No modificar el estado del juego.  
> 3. Responder con un mensaje error.  
> 4. Mantener o cerrar la conexión según la gravedad.

#### **Tabla de errores comunes**

| Código / Error | Cuándo ocurre | ¿Se cierra la conexión?   |
| :---- | :---- | :---- |
| INVALID_JSON | El texto recibido no es un JSON válido. | No, excepto si ocurre repetidamente. |
| UNKNOWN_TYPE | El campo type contiene un mensaje desconocido. | No. |
| MISSING_FIELD | Falta un campo obligatorio. | No. |
| INVALID_FIELD | Un campo tiene un valor o tipo incorrecto. | No. |
| INVALID_PHASE | La acción no está permitida en la fase actual. | No. |
| VERSION_MISMATCH | Cliente y servidor usan versiones incompatibles. | Sí. |
| LOBBY_FULL | El servidor alcanzó el máximo de jugadores. | Sí. |
| NAME_INVALID | El nombre está vacío, es muy largo o no es válido. | No. |
| MESSAGE_TOO_LARGE | El mensaje supera el tamaño máximo permitido. | Sí. |
| NOT_JOINED | El cliente intenta jugar antes de enviar join. | No. |

### 

### **5.2 Desconexiones**

| Situación | Acción del servidor | Estado de la bandera   |
| :---- | :---- | :---- |
| **Desconexión en lobby** | Eliminar jugador y actualizar lobby. | Sin cambios. |
| **Desconexión en countdown** | Eliminar jugador y comprobar mínimo. | Sin cambios. |
| **Desconexión durante el juego** | Eliminar jugador y continuar partida. | Sin cambios si no la tenía. |
| **Desconexión del portador** | Eliminar jugador. | Regresa a (500,500). |
| **Se desconectan todos** | Reiniciar la partida y volver al lobby. | Regresa a (500,500). |
| **Se desconecta el servidor** | Los clientes detienen la partida. | No existe estado oficial. |

#### 

#### **Casos especiales de desconexión**

> * **TCP close explícito:** Eliminar sesión. En PLAYING, el jugador queda inmóvil y se retira inmediatamente.  
> * **Timeout (8 s):** Mismo tratamiento que close: log reason=timeout.  
> * **Portador se desconecta:** Bandera vuelve al centro, mode=ground, owner=null (o 0) y version++.  
> * **Servidor se desconecta:** Clientes muestran SERVER_DISCONNECTED; no hay host migration.  
> * **Cliente intenta volver:** Rechazo durante PLAYING. Reconnection queda fuera de v1.

### **5.3 Decisiones de empate y propiedades del dominio**

| Caso | Regla autoritativa | Resultado observable   |
| :---- | :---- | :---- |
| **Dos capturan bandera libre** | Primer intento válido por arrival_ordinal; tie imposible, player_id fallback. | Uno recibe flag_changed; el otro queda stale. |
| **Varios roban al mismo portador** | Solo un cambio por flag_version. | Después del primer robo, deben pulsar otra vez. |
| **Intento duplicado** | action_id cacheado 10 s (idempotente). | No cambia dos veces ni incrementa versión. |
| **Cruce ganador y robo en mismo tick** | Movimiento y victoria se evalúan antes de interacciones. | El portador que cruzó gana; no se procesa robo. |
| **Mensaje viejo** | input_seq / sequence menor o igual se descarta. | No rebobina movimiento. |
| **Frame parcial** | Buffer espera bytes restantes. | No se considera comando hasta frame completo. |
| **Cliente lento** | Eventos se conservan; snapshots pendientes se coalescen al último. | No crece memoria: puede saltar estados visuales. |
| **Robo desde fuera** | Nuevo owner debe registrar transición no-fuera → fuera. | No gana instantáneamente. |

#### 

#### **Propiedades que deben permanecer verdaderas**

> 1. En cualquier tick existe como máximo un owner_id no cero / no nulo.  
> 2. flag_version nunca disminuye ni se repite después de un cambio.  
> 3. winner_id solo se escribe una vez.  
> 4. Un request_id / action_id aplicado no vuelve a mutar estado.  
> 5. Un cliente no puede ganar enviando coordenadas o match_ended.  
> 6. La misma secuencia de comandos ordenados produce el mismo resultado del dominio.