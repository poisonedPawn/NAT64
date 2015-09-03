---
language: es
layout: default
category: Documentation
title: Fragmentos Atómicos
---

[Documentation](documentation.html) > [Herramienta de configuración de Jool](documentation.html#Aplicacion-de-espacio-de-usuario) > [Flags](usr-flags.html) > [\--global](usr-flags-global.html) > Fragmentos Atómicos

# Fragmentos Atómicos

## Índice

1. [Introducción](#overview)
2. [Parámetros](#flags)
	1. [`--allow-atomic-fragments`](#atomicfragments)
	2. [`--setDF`](#setdf)
	3. [`--genFH`](#genfh)
	4. [`--genID`](#genid)
	5. [`--boostMTU`](#boostmtu)

## Introducción

Los "Fragmenos Atómicos" son por decirlo de otra manera "Fragmentos Aislados"; es decir, son paquetes de IPv6 que poseen un _fragment header_ sin que éste realmente sea un segmento de un paquete mayor. Este tráfico de fragmentos es permitido entre los saltos, _hops_, para el envío de información entre IPv6 e IPv4. Por lo general, estos paquetes son enviados por _hosts_ que han recibido un mensaje de error del tipo ICMPv6 "Packet too Big" para advertir que el próximo equipo, ya sea ruteador, hub, etc., soporta un MTU inferior al mínimo en IPv6, o sea que, el Next-Hop MTU es menor a 1280 bytes. Hay que recordar, que hasta el día de hoy entre redes Ethernet en IPv6 se suele emplear su valor máximo que es de 1500 bytes, pero en IPv4 el máximo ha variado con el tiempo y depende del medio y del protocolo por el cual se esté comunicando. En IPv6, por regla, el nodo origen es quien tiene la obligación de fragmentar el paquete y no los equipos que enlazan la red, cosa que si es permitido en IPv4. Para más información sobre las cabeceras de fragmento, [ver RFC. 2460, sección 4.5, 1998](https://tools.ietf.org/html/rfc2460#section-4.5). 

Sin embargo, esta implementación es vulnerable a infiltraciones, y algún _hacker_ puede tomar ventaja de la diferencia entre el MTU mínimo de IPv4, que es de 68 bytes, y el de IPv6, que es de 1280 bytes, para introducir fragmentos y generar problemas. Algunas referencias sobre este tema son:

[2010, RFC. 5927](https://tools.ietf.org/html/rfc5927)<br />
[2012, Security Implications of Predictable Fragment Identification Values](http://www.si6networks.com/presentations/IETF83/fgont-ietf83-6man-predictable-fragment-id.pdf)<br />
[2013, RFC. 6946](https://tools.ietf.org/html/rfc6946)<br />

La IETF está tratando de normar el [desuso de los fragmentos atómicos]({{ site.draft-deprecate-atomfrag-generation }}). Incluso en el RFC 6145, que es el documento principal de SIIT, advierte sobre dichos [problemas de seguridad](http://tools.ietf.org/html/rfc6145#section-6).

DESDE la perspectiva de Jool, como no se ha oficializado su desuso, estos aún siguen siendo soportados. Pero es destacable mencionar, que hemos registrado problemas técnicos al permitir los fragmentos atómicos. El kernel de Linux es particularmente deficiente cuando se trata de cabeceras de fragmento, asi que si Jool está generando uno, Linux añade otro adicional.

[![Figure 1 - que podría salir mal?](../images/atomic-double-frag.png)](obj/atomic-double-frag.pcapng)

En **Jool 3.2 y en versiones anteriores** se evade esto al NO delegar la fragmentación al kernel; pero, el hacelo así nos introdujo otros problemas más sutiles.

Ahora en **Jool 3.3**, la configuración por omisión es  deshabilitar los fragmentos atómicos, lo cual **te recomendamos no cambies**.

Estamos totalmente de acuerdo con la [iniciativa de su desuso, 2014]({{ site.draft-deprecate-atomfrag-generation }}) y cuando se formalize, en breve, se omitirá en Jool. 
 
## Parámetros

		
### `--allow-atomic-fragments`
	
- Nombre: ***PERMITE LOS FRAGMENTOS ATÓMICOS***
- Tipo: ***Booleano***
- Valor por Omisión: ***Apagado(0)***
- Modos: ***SIIT & Stateful***
- Sentido de traducción: ***IPv4 -> IPv6 & IPv6 -> IPv4***

Esta bandera sumariza la acción de las otras cuatro banderas (setDF, genFH, genID y boostMTU) con el propósito de habilitar o deshabilitar la recepción y traducción de los fragmentos aislados, llamados _atómicos_.

Para HABILITARLO, sencillamente ejecute:

{% highlight bash %}
$(jool) --allow-atomic-fragments verdadero
{% endhighlight %}

Y esto es equivalente a:

{% highlight bash %}
$(jool) --setDF verdadero      #NO FRAGMENTES
$(jool) --genFH verdadero      #GENERA CABECERA DE FRAGMENTO
$(jool) --genID falso
$(jool) --boostMTU falso
{% endhighlight %}

Según lo establece el [RFC 6145, sección 6](http://tools.ietf.org/html/rfc6145#section-6) este sería el comportamiento mandatorio,  pero está siendo verificado por la IETF, ver Draft Deprecate Atomfrag Generation]({{ site.draft-deprecate-atomfrag-generation }}).

Para DESHABILITARLO, sencillamente ejecute:

{% highlight bash %}
$(jool) --allow-atomic-fragments falso
{% endhighlight %}

Y esto es equivalente a:

{% highlight bash %}
$(jool) --setDF falso    #FRAGMENTABLE
$(jool) --genFH falso    #NO INCLUYAS CABECERA DE FRAGMENTO
$(jool) --genID verdadero
$(jool) --boostMTU verdadero
{% endhighlight %}

**Reafirmando: Jool 3.3 opera de esta última forma; es decir, _NO_ deja pasar los fragmentos atómicos.**


NOTAS:<br />
(1) La separación de los cuatro parámetros existe por razones históricas en la implementación, mas en el avance del proyecto se ha visto que no tiene sentido manejarlos individualmente y que los otras posibilidades conviene que sean descartadas.<br />
(2) La relación entre `--setDF` y `--boostMTU` es delicada. Consulta abajo para más detalles.


### `--setDF`

- Nombre: ***NO FRAGMENTES***
- Tipo: ***Booleano***
- Valor por Omisión: ***Apagado(0)***
- Modos: ***SIIT & Stateful***
- Sentido de traducción: ***IPv6 -> IPv4***

Mediante esta bandera se puede establecer si el mensaje es candidato a ser fragmentado o no, en su tránsito de IPv6 a IPv4.

La lógica descrita en forma de pseudocódigo es:
          
	SI (el paquete entrante tiene una cabecera de fragmento):      #SI PAQ. ENTRANTE en IPv6 TIENE CABECERA DE FRAGMENTO?
		El parámetro DF del paquete saliente en será Falso.           #AVISA PAQ. SALIENTE en IPv4 es un FRAGMENTO
	De otra forma:                                                 #SI PAQ. ENTRANTE en IPv6 NO TIENE CABECERA DE FRAGMENTO?
		SI (--setDF == 1):                                            #SI LA BANDERA "NO FRAGMENTES" ESTÁ ENCENDIDA?
            El parámetro DF del paquete saliente será Verdadero.         #AVISA PAQ. SALIENTE en IPv4 NO está FRAGMENTADO <> Va Entero
		De otra forma:                                                #SI LA BANDERA "NO FRAGMENTES" ESTÁ APAGADA? (Es Fragmentable)
            SI (la longitud del paquete saliente es > 1260):             #SI PAQ. SALIENTE en IPv4 > 1260? (Rebasa el Mínimo MTU en IPv6)          
				El parámetro DF del paquete saliente será Verdadero.        #AVISA PAQ. SALIENTE en IPv4 NO está FRAGMENTADO <> Es Fragmentable pero NO va Fragmentado
			De otra forma:                                               #SI PAQ. SALIENTE en IPv4 <= 1260? <> Menor al Mínimo MTU en IPv6
				El parámetro DF del paquete saliente será Falso.            #AVISA PAQ. SALIENTE en IPv4 es un FRAGMENTO <> Primer Fragmento

NOTAS:<br />
(1) El valor mínimo de MTU en IPv6 es igual a 1280 bytes, si a este valor le quitamos el tamaño del encabezado en IPv6, que es 40, y le sumamos el de IPv4, que es 20, nos da 1260 bytes.<br />
(2) Para una mejor comprensión, ver [`--boostMTU`](#boostmtu) y revisar la [Sección 6 del RFC 6145](http://tools.ietf.org/html/rfc6145#section-6).


### `--genFH`

- Nombre: ***GENERA CABECERA DE FRAGMENTO IPV6***
- Tipo: ***Booleano***
- Valor por Omisión: ***Apagado (0)***
- Modos: ***SIIT & Stateful***
- Sentido de traducción: ***IPv4 -> IPv6***

En IPv4 no hay diferencia a nivel de encabezados entre un paquete fregmentado y otro que no lo es. Esto se establece mediante el campo de _Identification_ y el valor del _Fragment Offset_. Los segmentos de un mismo paquete son identificados con **un mismo número** y en el _Fragment Offset_ se guarda **a partir de que número de dato se está trasmitiendo**. Pero, en IPv6 si existe diferencia entre el primer segmento y el resto. En IPv6, sólo el 1er paq. incluye la cabecera a nivel 3 (ICMP, UDP, TCP) y en el resto se omite; es decir, ya no se repite como en IPv4.

A través de esta bandera se puede prever que en la transición de IPv4 a IPv6 se incluya o no una cabecera de fragmento.

La validación descrita en forma de pseudocódigo es:

	Si (--genFH == 1 && el paquete entrante tiene ** DF inactivo**):   #SI GENERA CABECERA en IPv6 && PAQ. ENTRANTE FRAGMENTABLE?
		Jool generará una cabecera de fragmento IPv6.                     #AGREGA en PAQ. SALIENTE de IPv6 CABECERA DE FRAGMENTO <> 2o Fragmento en delante
	Si (--genFH == 0):                                                 #SI NO GENERA CABECERA en IPv6?
		Si (paquete entrante es un fragmento):                            #SI PAQ. ENTRANTE en IPv4 es un FRAGMENTO?
			Jool generará una cabecera de fragmento IPv6.                    #AGREGA en PAQ. SALIENTE de IPv6 CABECERA DE FRAGMENTO <> 1er Fragmento
		De otra forma:                                                    #SI PAQ. ENTRANTE en IPv4 NO es un FRAGMENTO
			Jool NO generará una cabecera de fragmento IPv6.                 #PAQ. SALIENTE de IPv6 NO tiene CABECERA DE FRAGMENTO <> Paq. Completo
		
NOTAS:<br />
(1)Cuando `--genFH` está apagado **no importa** si el parámetro DF del paquete entrante nos dice que el paquete "no está fragmentado" o si "es fragmentable".<br />
(2)Este es el parámetro que causa que Linux se comporte erróneamente cuando necesita fragmentar. No funciona bien, así que actívalo bajo tu propio riesgo.


### `--genID`

- Nombre: ***GENERA IDENTIFICACIÓN IPV4***
- Tipo: ***Booleano***
- Valor por Omisión: ***Encendido (1)***
- Modos: ***SIIT & Stateful***
- Sentido de traducción: ***IPv6 -> IPv4***

Los paquetes IPv6 solo disponen de un campo de identificación  si son un fragmento; es decir, si tienen una cabecera de fragmento. Sin embargo, todos los paquetes de IPv4 deben de llevar un campo de identificación. Esta bandera sirve para especificarle a Jool qué hacer cuando vaya a traducir un fragmento de IPv6 a IPv4.

La lógica descrita en forma de pseudocódigo es:

	SI (el paquete entrante tiene una cabecera de fragmento):      #SI PAQ. ENTRANTE en IPv6 TIENE CABECERA DE FRAGMENTO?
		Copia los bits de orden más bajo del campo de                 #TRANSFIERE EL NO. IDENTIFICADOR de IPV6 a IPV4 
		identificación de la cabecera de fragmento en 
		IPv6 al de IPv4
	De otra forma:                                                 #SI PAQ. ENTRANTE en IPv6 NO TIENE CABECERA DE FRAGMENTO?
		SI (--genID == 0):                                            #SI LA BANDERA "GENERA IDENTIFICACIÓN IPV4" ESTÁ APAGADA?
            Selecciona el el campo de identificación                     #NO. IDENTIFICADOR de IPV4 = 0
			de la cabecera de fragmento de IPv4 = 0
		De otra forma:                                                #SI LA BANDERA "GENERA IDENTIFICACIÓN IPV4" ESTÁ ENCENDIDA?
            Selecciona el el campo de identificación                     #NO. IDENTIFICADOR de IPV4 = Número Aleatorio <> 1er Fragmento
			de la cabecera de fragmento de IPv4 = 
			Número Aleatorio

			
### `--boostMTU`

- Nombre: ***PROMUEVE MTU IPv6***
- Tipo: ***Booleano***
- Valor por Omisión: ***Encendido (1)***
- Modes: ***SIIT && Stateful***
- Dirección de traducción: ***IPv4 -> IPv6 (aplica en: msg. de error de ICMP)***

Como se mencionó en la introducción, decíamos que cuando un paquete es muy grande para el MTU de un enlace, los routers en IPv4 generan mensajes ICMP de error -[Fragmentation Needed](http://tools.ietf.org/html/rfc792)- en IPv4 que pudieran ser traducidos como  -[Packet too Big](http://tools.ietf.org/html/rfc4443#section-3.2)- en IPv6.

Estos errores ICMP se supone deben contener el MTU infractor para que el emisor pueda reajustar el tamaño de sus paquetes. Dado que el MTU mínimo para IPv4 es 68 bytes y el de IPv6 es 1280, Jool puede encontrarse queriendo reportar un MTU illegal en IPv6 al traducir un _Fragmentation Needed_ (v4) en un _Packet too Big_ (v6). Con `--boostMTU` se trata de evitar este tipo de falla.

Jool requiere hacer un pequeño ajuste al comparar los MTUs, por la diferencia entre la longitud básica del header IPv4(20 bytes) y la del header IPv6(40 bytes). Un paquete en IPv6 puede ser 20 bytes más grande que el MTU de IPv4 por que va a perder 20 bytes cuando su cabecera IPv6 sea reemplazada por una de IPv4.

La validación descrita en forma de pseudocódigo es:

	IPv6_error.MTU = IPv4_error.MTU + 20
	SI (--boostMTU == 1 && IPv6_error.MTU < 1280):   		#SI LA BANDERA "PROMUEVE MTU" ESTÁ ENCENDIDA && LONGITUD DEL PAQ. ENTRANTE ES MENOR A LA LONG. MÍNIMA PERMITIDA EN IPV6?
		MTU de IPv6 = 1280                                     #ASIGNA A MTU de IPV6 SU VALOR MÍNIMO
	
En cualquier otro caso, con `--boostMTU` deshabilitado y/o IPv6_error.MTU >= 1280 , Jool no alterará este campo. 

Para mayor información vea la [sección 6 del RFC 6145](http://tools.ietf.org/html/rfc6145#section-6). Alli se describen los fundamentos básicos.

**AVISO:**

Si `--setDF` && `--boostMTU` están ambos en ENCENDIDO (1) y hay un enlace IPv4 con MTU < 1260, se llega a establecer un bucle infinito similar al [MTU hassle](mtu.html):

1. El emisor IPv6 transmite un paquete de tamaño 1280.
2. Jool lo traduce en un paquete IPv4 de tamaño 1260 con DF=1
3. Un router IPv4 con interfaz de salida con MTU < 1260 genera _ICMPv6 Frag Needed_ con MTU=1006 (o algún otro).
4. Jool lo traduce a ICMPv6 _Packet Too Big_ con MTU=1280.
5. Ve al punto 1.

Extendemos un agradecimiento a Tore Anderson por darse cuenta de ello y reportar esta peculiaridad. 