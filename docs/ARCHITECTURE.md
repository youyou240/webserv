# Le cœur du serveur — l'histoire d'une requête

> Ce document raconte, pas à pas, **ce qui se passe vraiment** quand notre serveur démarre et qu'un client s'y connecte.
> Pas besoin de connaissances préalables : on suit le code dans l'ordre où il s'exécute, on cite à chaque fois le fichier où ça se passe, et on s'arrête sur les passages importants.
> Tout ce qui est décrit ici est **la fondation** du serveur : ça ne bougera plus. Les features HTTP viendront se poser dessus, on en parle à la fin.

---

## Les personnages de l'histoire

Avant de lever le rideau, présentons les acteurs. Ils sont peu nombreux, et c'est voulu.

- **`Epoll`** *(Epoll/epollClass.cpp)* — le **multiplexeur** d'I/O. Il y en a **un seul**. Il attend que des FD soient prêts (`epoll_wait`) et expose `add`/`mod`/`del`. Il ne connaît ni la config ni les sockets — juste les FD à surveiller.
- **`ServerManager`** *(Server/ServerManagerClass.cpp)* — le **chef d'orchestre**. Il possède les `Server` (blocs config) et les `Socket` (endpoints), construit la topologie (`build`), tient la boucle d'événements (`run`) en s'appuyant sur l'`Epoll`, et le menu d'admin.
- **`Arole`** *(Server/AroleClass/AroleClass.cpp)* — une **classe abstraite**. Elle dit : « tout objet surveillé par `Epoll` doit savoir réagir à son propre événement ». Elle ne sait pas *comment*, elle impose juste le contrat.
- **`Acceptor`** *(Server/AroleClass/AcceptorClass.cpp)* — un `Arole` concret. Sa seule mission : **accepter les nouvelles connexions** sur un port.
- **`Client`** *(Server/AroleClass/ClientClass.cpp)* — un autre `Arole` concret. Il représente **un visiteur connecté** : il lit ce qu'on lui envoie, il répond, puis il s'en va.
- **`Socket`** *(Server/socket/socketClass.cpp)* — un **endpoint** d'écoute (un `host:port`, un `bind`/`listen`). Possédé par le `ServerManager`.
- **`Server`** *(Server/ServerClass.cpp)* — **un bloc `server` du fichier de config** (un virtual host) : il porte sa `ServerConfig` et vit sur un ou plusieurs endpoints.
- **`ConfigParser`** *(Config/ConfigParserClass.cpp)* — le cartographe. Il lit le fichier de configuration et en tire une carte que tout le reste va suivre.
- **`ServerConfig` / `LocationConfig`** *(Config/ServerConfigClass.cpp, Config/LocationConfigClass.cpp)* — la carte elle-même : des sacs de données décrivant chaque site et chacune de ses routes.
- **`HttpRequest`** *(Http/HttpRequestClass.cpp)* — l'**oreille** du serveur : une machine à états qui digère les octets reçus au fur et à mesure et sait dire « la requête est complète » ou « elle est invalide, voilà le code d'erreur ».
- **`HttpResponse`** *(Http/HttpResponseClass.cpp)* — la **voix** : il assemble status line, headers (`Content-Length`, `Date`, `Server`…) et body en une réponse HTTP sérialisée, avec les pages d'erreur par défaut.

Retenez surtout cette phrase, tout le reste en découle :

> **`Acceptor` et `Client` sont tous les deux des `Arole`.** Donc `Epoll` peut les traiter de la même manière, sans savoir lequel est lequel.

---

## Acte 0 — Avant le lever de rideau : lire la carte

Avant d'ouvrir le moindre port, le serveur a besoin de savoir **quoi servir, où, et comment**. Cette information vit dans un fichier de configuration (`configs/default.conf` par défaut), et c'est la toute première chose que `main.cpp` traite :

```cpp
ConfigParser parser(path);
parser.parse();
std::cout << parser << std::endl;
```

*(main.cpp)*

`path` vient de la ligne de commande (`./webserv [config]`), ou vaut le fichier par défaut si on ne passe rien. À la sortie de `parse()`, on tient une **carte du serveur** : un `std::vector<ServerConfig>`. Voyons comment le texte devient cette carte.

### Deux passes : d'abord des jetons, ensuite du sens

Le parseur ne cherche pas le sens caractère par caractère. Il procède en **deux temps**, comme on lit une phrase : on découpe d'abord en mots, on comprend la grammaire ensuite.

**Passe 1 — le lexer** transforme le texte en une liste de **jetons** (`Token`). Trois caractères sont « structurels » et forment chacun un jeton — `{`, `}`, `;` — tout le reste est un **mot**. Les commentaires (`#` jusqu'à la fin de ligne) sont jetés, et chaque jeton retient **son numéro de ligne**, pour les messages d'erreur.

```cpp
Token tok;
tok.type = T_WORD;
tok.value = content.substr(start, i - start);
tok.line = line;
_tokens.push_back(tok);
```

*(Config/ConfigParserClass.cpp)*

Ainsi `location /uploads {` devient trois jetons : `location` (mot), `/uploads` (mot), `{` (structurel) — et le lexer ne se demande pas encore si c'est valide.

### Passe 2 — la descente récursive

Avec les jetons en main, on les lit par **descente récursive** : une fonction par niveau de grammaire, qui s'appellent en cascade.

- `parseServer` attend `server`, puis `{`, puis une suite d'items jusqu'à `}`.
- Chaque item est soit un bloc `location` (→ `parseLocation`), soit une **directive** (→ `parseDirective`).
- `parseDirective` lit le nom, collecte ses arguments jusqu'au `;`, puis **aiguille** vers le bon handler :

```cpp
if (key.value == "listen")
    handleListen(server, location, args, key.line);
else if (key.value == "root")
    handleRoot(server, location, args, key.line);
...
else
    fail("unknown directive '" + key.value + "'", key.line);
```

*(Config/ConfigParserClass.cpp)*

Chaque handler est **strict** : il vérifie le nombre d'arguments, le **placement** (un `listen` dans une `location` est refusé, un `upload_store` au niveau server aussi) et la validité des valeurs (port numérique 1–65535, taille `1m`/`512k`, code 100–599). À la moindre entorse, `fail(...)` lève une exception qui porte **le numéro de ligne** :

```
Config error (line 14): unknown directive 'autoindexx'
```

C'est du **fail-fast** : une config douteuse arrête le démarrage immédiatement, plutôt que de produire un comportement surprenant plus tard.

### L'héritage : la carte se complète toute seule

Réécrire `root` et `index` dans **chaque** `location` serait pénible. D'où l'héritage : le `server` fournit les valeurs par défaut, une `location` n'écrit que ce qu'elle veut **changer**. La résolution se fait en fin de parse :

```cpp
void LocationConfig::inheritFrom(ServerConfig const & server)
{
    if (!hasRoot)
        root = server.root;
    if (!hasAutoindex)
        autoindex = server.autoindex;
    ...
}
```

*(Config/LocationConfigClass.cpp)*

Le détail subtil — celui qu'on explique en soutenance — est le `if (!hasRoot)`. Pour un booléen comme `autoindex`, `false` voudrait dire à la fois « pas écrit » **et** « écrit `off` » : impossible de distinguer. La solution : chaque champ héritable porte un **drapeau « a été écrit ? »** (`hasRoot`, `hasAutoindex`…), levé par le handler quand la directive apparaît. À la résolution, on n'hérite **que si le drapeau est baissé**. Ensuite, chaque `LocationConfig` est **autonome** : le router n'aura jamais à remonter vers le `server`.

### Les virtual hosts : plusieurs sites, une seule porte

Rien n'interdit à deux blocs `server` d'écouter le **même** `host:port` :

```
server { listen 8080; server_name localhost;    root ./www;   }
server { listen 8080; server_name site-b.local; root ./www-b; }
```

C'est le mécanisme des **virtual hosts** : un seul port physique, plusieurs sites. Lequel répond ? Celui dont le `server_name` correspond au header `Host` de la requête (le premier sert de défaut). Cette **sélection** se fera à la requête, côté HTTP ; pour l'instant, retenez juste que la carte peut porter plusieurs sites sur un même endpoint.

### Le passage de relais

`parse()` rend son `std::vector<ServerConfig>`. `main` le confie à un **`ServerManager`**, qui construit **un `Server` par bloc** et les **sockets partagés** — le rideau peut se lever :

```cpp
manager.build(parser.getServers());
```

---

## Acte 1 — Le serveur s'éveille

Une fois la carte lue (acte 0), `main.cpp` donne vie au serveur en quelques lignes :

```cpp
Epoll & epoll = Epoll::createEpoll();
ServerManager manager(epoll);
std::signal(SIGINT, ServerManager::handleSigEvent);
manager.build(parser.getServers());
manager.run();
```

Suivons-les une par une.

### `Epoll::createEpoll()` — naître une seule fois

```cpp
Epoll& Epoll::createEpoll(void)
{
    static Epoll epoll;
    return epoll;
}
```

*(Epoll/epollClass.cpp)*

Le mot-clé `static` ici est malin : la variable `epoll` n'est créée **qu'une seule fois**, à la première entrée dans la fonction. Tous les appels suivants renvoient le **même** objet. C'est notre garantie qu'il n'existe **qu'un seul `Epoll`** — exactement ce que le sujet exige (« un seul `poll()` pour toutes les I/O »).

À sa naissance, l'`Epoll` demande au noyau de lui créer son instance de surveillance :

```cpp
Epoll::Epoll(void) : _events(MIN_MAX)
{
    _fd = epoll_create(1);
    if (_fd == -1)
        throw epollError();
}
```

*(Epoll/epollClass.cpp)*

`epoll_create` renvoie un **descripteur de fichier** (`_fd`) qui représente « la liste des choses que je surveille ». `_events` est un tableau qui servira de boîte aux lettres : c'est là que le noyau déposera les événements quand il y en aura.

### `manager.build(...)` — ouvrir les portes

`main` confie la carte au `ServerManager`. Pour **chaque bloc** `server` du config, on crée **un objet `Server`** (un virtual host), possédé par le `ServerManager`. Puis, pour chacun de ses `listen`, on récupère le socket de l'endpoint et on s'y enregistre :

```cpp
Server *srv = new Server(configs[i]);
addServer(srv);
for (chaque (host, port) de configs[i].listen)
{
    Socket *sock = getOrCreateSocket(host, port);
    sock->addServer(srv);
}
```

*(Server/ServerManagerClass.cpp)*

Le cœur, c'est `getOrCreateSocket`. Le `ServerManager` tient une `std::map<host:port, Socket*>` qui est l'**unique propriétaire** des sockets. Si l'endpoint existe déjà, on **réutilise** le socket ; sinon on en crée un :

```cpp
std::map<std::string, Socket*>::iterator it = _sockets.find(key);
if (it != _sockets.end())
    return (it->second);
Socket *sock = new Socket(host, port);
_sockets.insert(std::make_pair(key, sock));
sock->start(_epoll);
return (sock);
```

*(Server/ServerManagerClass.cpp)*

C'est ainsi que les **virtual hosts** (acte 0) se résolvent : deux blocs `server` sur `0.0.0.0:8080` tombent sur le **même** `Socket` ; le second fait juste `sock->addServer(srv)` pour s'ajouter à la liste des sites de cet endpoint — **sans nouveau `bind`**. La séparation des rôles est nette :

- le **`ServerManager`** possède les `Server` (les blocs config) **et** les `Socket` (les endpoints) ;
- l'**`Epoll`** ne possède rien de tout ça : il surveille juste les FD ;
- un **`Server`** ne possède aucun socket : il vit sur les endpoints où il écoute, et l'`Acceptor` de chaque endpoint garde la liste des `Server` (via `addServer`) pour choisir le bon site selon le header `Host`.

Quand on crée vraiment un `Socket`, c'est dans son constructeur que se joue la mécanique réseau :

```cpp
Socket::Socket(std::string const & host, uint16_t port)
{
    int opt = 1;
    _addr.resolve(host, port);
    _sockfd = socket(AF_INET, SOCK_STREAM, 0);
    ...
    setsockopt(_sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    bind(_sockfd, (struct sockaddr *)&_addr.getStruct(), sizeof(struct sockaddr));
    _acceptor.setFD(_sockfd);
    listen(_sockfd, SOMAXCONN);
}
```

*(Server/socket/socketClass.cpp)*

La séquence fondatrice (chaque appel est testé, FD fermé avant de relancer en cas d'échec) :

1. **`_addr.resolve(host, port)`** — traduit `host:port` en adresse via **`getaddrinfo`** ; host vide → `AI_PASSIVE` → `0.0.0.0` (toutes les interfaces), sinon l'interface précise écrite dans la config.
2. **`socket()`** — une prise réseau, on récupère un FD.
3. **`fcntl(..., O_NONBLOCK)`** — non-bloquante : sans ça, un `recv` pourrait figer tout le serveur.
4. **`setsockopt(SO_REUSEADDR)`** — pour **rouvrir le port aussitôt** après un arrêt (sinon il reste en `TIME_WAIT` et le redémarrage échoue avec `EADDRINUSE`).
5. **`bind()`** puis **`listen()`** — le port est ouvert.

Enfin `_acceptor.setFD(_sockfd)` confie le FD d'écoute à l'`Acceptor`, qui sera chargé de tout ce qui arrive dessus.

### `manager.run()` — entrer dans la danse

Le serveur est armé, les ports sont ouverts. Mais personne ne surveille encore rien. C'est le rôle de `run()`. On y arrive à l'acte suivant — mais avant, il faut que les `Acceptor` se signalent à l'`Epoll`. Ça se fait via `Socket::start` :

```cpp
void Socket::start(Epoll & manager)
{
    _acceptor.start(manager);
    ...
}
```

```cpp
void Acceptor::start(Epoll & manager)
{
    manager.add(this);
    this->_active = true;
}
```

*(Server/socket/socketClass.cpp et Server/AroleClass/AcceptorClass.cpp)*

`manager.add(this)` : l'`Acceptor` dit à l'`Epoll` **« surveille-moi »**. Et c'est ici qu'on rencontre l'astuce centrale de tout le projet.

---

## Acte 2 — L'astuce qui fait tout tenir : `data.ptr`

Quand un `Acceptor` se construit, il prépare une petite carte d'identité :

```cpp
Acceptor::Acceptor(void) : Arole(), _active(false)
{
    this->_acceptor += 1;
    this->_ev.events = EPOLLIN;
    this->_ev.data.ptr = this;
}
```

*(Server/AroleClass/AcceptorClass.cpp)*

Deux champs à comprendre dans ce `_ev` (un `struct epoll_event`) :

- **`_ev.events = EPOLLIN`** : « je veux être prévenu quand il y a quelque chose à **lire** » (pour un acceptor, « à lire » = « une connexion attend »).
- **`_ev.data.ptr = this`** : « et quand tu me préviens, redonne-moi **ce pointeur** ». Ce pointeur, c'est l'objet lui-même.

Quand `manager.add(this)` est appelé, voici ce qui part vers le noyau :

```cpp
void Epoll::add(Arole * role)
{
    Client *conv;
    if (epoll_ctl(_fd, EPOLL_CTL_ADD, role->getFD(), &role->getStructEvent()) == -1)
    {
        if ((conv = dynamic_cast<Client *> (role)))
            conv->closeConnection(*this);
    }
}
```

*(Epoll/epollClass.cpp)*

`epoll_ctl(..., EPOLL_CTL_ADD, ...)` enregistre le FD **et** sa carte d'identité (`_ev`, qui contient le `this`). Le noyau va maintenant garder un œil dessus, et il a mémorisé notre pointeur.

**Pourquoi c'est génial ?** Parce que le jour où ce FD devient prêt, le noyau nous rendra ce pointeur. On n'aura pas à se demander « est-ce un acceptor ou un client ? ». On récupérera un `Arole *`, on appellera sa méthode, et le polymorphisme C++ fera le bon choix tout seul. On le voit à l'acte suivant.

---

## Acte 3 — Le grand sommeil, et le réveil

Voici le cœur battant du serveur, la boucle d'événements :

```cpp
void ServerManager::run(void)
{
    while (this->_active)
    {
        if (isRunning)
        {
            int num_events = _epoll.wait();
            std::vector<struct epoll_event> const & events = _epoll.getEvents();
            for (int i = 0; i < num_events; ++i)
            {
                Arole *role = static_cast<Arole *>(events[i].data.ptr);
                if (role == NULL)
                    continue ;
                role->handle_event(_epoll, events[i].events);
            }
            Arole::launchActiveRoutine(_epoll);
        }
        else
            sigManager();
    }
}
```

*(Server/ServerManagerClass.cpp)*

La boucle vit dans le `ServerManager`, mais elle **délègue le sommeil** à l'`Epoll` : `_epoll.wait()` est le seul `epoll_wait` du programme, et `getEvents()` expose le tampon rempli par le noyau.

Décortiquons le moment le plus important : `wait()`.

```cpp
int Epoll::wait(void)
{
    int events = epoll_wait(_fd, _events.data(), _events.size(), EPOLL_TIMEOUT_MS);
    if (events == -1)
    {
        if (errno == EINTR)
            return (0);
        throw epollError();
    }
    return (events);
}
```

*(Epoll/epollClass.cpp)*

`epoll_wait` est **le seul endroit où le programme dort**. Il dit au noyau : « réveille-moi seulement quand au moins un de mes FD surveillés a quelque chose à faire — ou au bout de `EPOLL_TIMEOUT_MS` (5 secondes), même s'il ne se passe rien ». Tant que le serveur est inactif, il ne consomme **quasiment aucun** CPU : il attend.

Pourquoi ce timeout de 5 s alors qu'on pourrait attendre indéfiniment ? Parce qu'on veut **périodiquement vérifier les connexions trop vieilles** (on y revient à l'acte 8) : le gardien du temps ne tourne qu'au réveil de la boucle. Avec des deadlines de 30 s sur les headers, un réveil toutes les 60 s les ferait déborder du double ; toutes les 5 s, elles sont respectées à une marge près, pour un coût dérisoire (un réveil à vide toutes les 5 s ne fait rien de plus que reboucler).

Quand `epoll_wait` revient, `_events` contient les FD prêts. La boucle les parcourt et, pour chacun :

```cpp
Arole *role = static_cast<Arole *>(_events[i].data.ptr);
role->handle_event(*this, _events[i].events);
```

On récupère le `this` qu'on avait confié au noyau (acte 2), on le voit comme un `Arole *`, et on appelle `handle_event`. Si c'était un `Acceptor`, c'est `Acceptor::handle_event` qui s'exécute ; si c'était un `Client`, c'est `Client::handle_event`. **Magie du polymorphisme** : un seul appel, deux comportements, zéro `if` pour distinguer les types.

Et on passe `_events[i].events` en deuxième argument. **Souvenez-vous de ce détail**, c'est tout l'objet de l'acte 5.

---

## Acte 4 — Un visiteur frappe à la porte

Un client se connecte sur le port. Le noyau réveille `epoll_wait`, qui rend l'`Acceptor` comme prêt en lecture. La boucle appelle donc :

```cpp
void Acceptor::handle_event(Epoll & manager, uint32_t)
{
    int fd = accept(_fd, NULL, NULL);
    if (fd == -1)
        return ;
    Client *client = new Client(fd, *this);
    if (fcntl(fd, F_SETFL, O_NONBLOCK) == -1)
    {
        client->closeConnection(manager);
        return ;
    }
    manager.add(client);
}
```

*(Server/AroleClass/AcceptorClass.cpp)*

Étape par étape :

1. **`accept()`** prend la connexion en attente et renvoie un **nouveau FD**, dédié à ce client précis (le FD d'écoute, lui, continue d'écouter les suivants).
2. On crée un **`Client`** pour ce FD.
3. On rend ce FD **non-bloquant** (`O_NONBLOCK`), pour les mêmes raisons que pour le socket d'écoute.
4. **`manager.add(client)`** : on demande à l'`Epoll` de surveiller ce client aussi.

À la construction du `Client`, il s'enregistre lui-même et prépare sa carte d'identité, comme l'acceptor :

```cpp
Client::Client(int fd, Acceptor const & parent)
    : Arole(fd), _acceptor(&parent), _acceptorfd(parent.getFD()), _bytes_send(0),
      _draining(false), _closeAfterWrite(false), _limitApplied(false),
      _lastActivity(time(NULL)), _requestStart(0)
{
    this->_client += 1;
    _activeClient[_acceptorfd].insert(std::pair<int, Client *>(_fd, this));
    _ev.events = EPOLLIN;
    _ev.data.ptr = this;
}
```

*(Server/AroleClass/ClientClass.cpp)*

`_ev.events = EPOLLIN` : pour l'instant, ce client nous intéresse **en lecture** (on attend qu'il nous parle). `_ev.data.ptr = this` : encore notre carte d'identité. Le client garde un pointeur vers **son `Acceptor`** : c'est par lui qu'il connaît les virtual hosts de son endpoint (nécessaire pour choisir la bonne limite de body — et demain, pour le routing). Enfin, deux horloges démarrent : `_lastActivity` (dernière activité) et `_requestStart` (début de la requête en cours, `0` = pas de requête en cours) — leurs rôles distincts sont expliqués à l'acte 8.

À partir de maintenant, l'`Epoll` surveille deux choses : l'acceptor (pour les prochains visiteurs) **et** ce client.

---

## Acte 5 — Le client parle, et le piège qu'on évite

Le client envoie sa requête. `epoll_wait` se réveille, rend ce client comme prêt en lecture, et la boucle appelle `Client::handle_event` :

```cpp
void Client::handle_event(Epoll & manager, uint32_t events)
{
    if (events & (EPOLLERR | EPOLLHUP))
    {
        this->closeConnection(manager);
        return ;
    }
    if (events & EPOLLIN)
    {
        if (!this->handleRead(manager))
            return ;
    }
    if (events & EPOLLOUT)
        this->handleWrite(manager);
}
```

*(Server/AroleClass/ClientClass.cpp)*

Ici se cache **le piège le plus important de tout le projet**, et il faut bien comprendre la différence entre deux choses qui s'appellent presque pareil :

| Le nom | Qui le remplit | Ce que ça veut dire |
|---|---|---|
| **`_ev.events`** | **nous** | « ce qui m'**intéresse** » (ce qu'on a demandé à surveiller) |
| **`events`** (le paramètre) | **le noyau**, via `epoll_wait` | « ce qui s'est **vraiment produit** maintenant » |

Le réflexe naïf serait de tester `_ev.events`. **Erreur** : ça revient à demander « qu'est-ce que je voulais surveiller ? » au lieu de « qu'est-ce qui est prêt ? ». C'est pour ça que `handle_event` reçoit `events` en paramètre, depuis la boucle (acte 3) — et qu'on teste **lui**.

Le noyau peut aussi nous rendre des événements qu'on n'a **jamais demandés** : `EPOLLERR` (erreur) et `EPOLLHUP` (le client a coupé la connexion). On les teste **en premier** : si ça part en vrille, inutile d'essayer de lire ou écrire, on ferme.

Notez aussi les deux `if` séparés (et non un `else if`) pour `EPOLLIN` et `EPOLLOUT` : un FD peut être prêt en lecture **et** en écriture en même temps. Le sujet impose justement de pouvoir surveiller les deux simultanément.

Voyons maintenant la lecture :

```cpp
bool Client::handleRead(Epoll & manager)
{
    char buffer[MAX_REQUEST];
    int bytes_read = recv(this->_fd, buffer, sizeof(buffer), 0);

    if (bytes_read == 0)
    {
        if (_draining)
        {
            _ev.events = EPOLLOUT;
            manager.mod(this);
            return (false);
        }
        this->closeConnection(manager);
        return (false);
    }
    if (bytes_read < 0)
        return (true);
    _lastActivity = time(NULL);
    if (_draining)
        return (true);
    if (_requestStart == 0)
        _requestStart = time(NULL);
    _request.append(buffer, bytes_read);
    ...
}
```

*(Server/AroleClass/ClientClass.cpp)*

Trois cas après `recv`, et **on ne regarde jamais `errno`** (le sujet l'interdit après une lecture/écriture) :

- **`bytes_read == 0`** : le client a fermé son côté écriture. (C'est une **valeur de retour**, pas `errno` : autorisé.) Deux sous-cas : en temps normal, plus rien à attendre → on ferme, et on renvoie `false` pour signaler « ce client n'existe plus, n'y touche pas ». Mais si on est en train de **drainer** (voir plus bas), une réponse d'erreur attend encore d'être livrée : on bascule en `EPOLLOUT` seul pour l'envoyer d'abord — sans ça, un client qui fait un *half-close* (il a fini d'envoyer et attend notre réponse, comportement de `nc` sur macOS) ne recevrait jamais son `400`/`413`.
- **`bytes_read < 0`** : pas de données disponibles pour l'instant. On ne s'affole pas, on ne ferme pas : on renvoie simplement la main, on réessaiera quand le noyau nous re-préviendra. Et si c'était une vraie erreur réseau ? Elle reviendra sous forme de `EPOLLERR`/`EPOLLHUP`, traités en haut de `handle_event`. C'est ce mécanisme qui **remplace** la lecture d'`errno`.
- **`bytes_read > 0`** : on note l'activité (`_lastActivity`), on démarre l'horloge de la requête si c'est son premier octet (`_requestStart`), et on **nourrit le parseur** : `_request` n'est plus une simple chaîne, c'est un objet `HttpRequest` — une machine à états qui digère les octets au fur et à mesure.

### La machine à états `HttpRequest` — savoir quand la requête est finie

Une requête peut arriver en un morceau, en cinquante, ou coupée au milieu d'un header : `recv` rend ce que le réseau veut bien donner. Le parseur *(Http/HttpRequestClass.cpp)* accumule donc dans un buffer interne et avance d'état en état, en ne consommant que ce qui est complet — jamais il n'attend, jamais il ne bloque :

```
S_REQUEST_LINE → S_HEADERS → S_BODY (Content-Length) ─────────────→ S_COMPLETE
                       └────→ S_CHUNK_SIZE ⇄ S_CHUNK_DATA → S_TRAILER ──↗
                                              (à tout moment → S_ERROR)
```

- **Request line** : méthode, URI (path percent-décodé, query séparée), version. Validation stricte et immédiate : syntaxe cassée → `400`, version inconnue → `505`, méthode non gérée → `501`, ligne trop longue → `414`. C'est du **fail-fast** : une première ligne irrécupérable est refusée sans attendre la suite.
- **Headers** : rangés dans une `map` (clés en minuscules), `Host` obligatoire en HTTP/1.1, doublons interdits sur `Host`/`Content-Length`/`Transfer-Encoding` (anti request-smuggling), taille totale plafonnée (`MAX_HEADER_BYTES`).
- **Body** : soit `Content-Length` octets, soit **dé-chunking** (`Transfer-Encoding: chunked`) fait ici une fois pour toutes — le CGI recevra un body déjà propre, comme l'exige le sujet.
- `client_max_body_size` est vérifié **pendant** la lecture : dès le `Content-Length` annoncé, ou chunk par chunk. On sait donc dire `413` sans avoir lu 2 Go pour rien (bonus observé en test : curl envoie `Expect: 100-continue` et attend avant de pousser le body → refusé dès les headers, zéro octet de body transféré).

Deux issues possibles : `isComplete()` (on peut répondre) ou `isError()` (le code de la réponse est déjà décidé). La suite de `handleRead` aiguille :

```cpp
if (!_limitApplied && _request.headersDone())
{
    _request.setMaxBodySize(this->resolveMaxBodySize());
    _limitApplied = true;
}
if (_request.isError())
{
    this->genErrorResponse(_request.getErrorCode());
    _draining = true;
    _closeAfterWrite = true;
    _ev.events = EPOLLIN | EPOLLOUT;
    manager.mod(this);
    return (false);
}
if (_request.isComplete())
{
    this->genResponse();
    _closeAfterWrite = !_request.isKeepAlive();
    _ev.events = EPOLLOUT;
    manager.mod(this);
    return (false);
}
return (true);
```

*(Server/AroleClass/ClientClass.cpp)*

Trois choses importantes ici :

1. **La limite de body dépend du site visé.** Dès que les headers sont complets, on lit `Host`, on choisit le virtual host parmi ceux de l'endpoint (`selectVhost`, la sélection annoncée à l'acte 0) et on applique **son** `client_max_body_size`. (Le raffinement par `location` viendra avec le routing.)
2. **Requête complète** → on prépare la réponse (`HttpResponse`) et on bascule l'intérêt vers `EPOLLOUT`, comme avant. `_closeAfterWrite` mémorise ce qu'il faudra faire après l'envoi, selon le header `Connection` (keep-alive ou close).
3. **Requête en erreur** → le **drain** : on demande `EPOLLIN | EPOLLOUT` **en même temps**. Pourquoi ne pas juste répondre ? Parce que le client est peut-être en plein milieu de l'envoi de son gros body. Si on arrête de lire, ses buffers se remplissent, son envoi bloque, et il ne lira peut-être jamais notre `413` : deadlock. On continue donc de lire **en jetant tout** (`_draining` : les octets suivants sont lus puis ignorés) pendant qu'on pousse la réponse — puis on ferme. C'est le cas « lecture et écriture simultanées » que le sujet exige de savoir gérer.

> Le `return (false)` après une fermeture n'est pas un détail : dès qu'une connexion est fermée, l'objet `Client` est **détruit** (acte 7). Le `false` permet à `handle_event` de s'arrêter net et de ne plus jamais toucher l'objet. Et comme `manager.mod(...)` peut lui-même fermer la connexion en cas d'échec, **règle d'or : tout appel qui peut fermer la connexion (`closeConnection`, `manager.add/mod/del`) est immédiatement suivi d'un `return`.**

---

## Acte 6 — Le serveur répond (et le piège de l'envoi partiel)

Le client est maintenant prêt en écriture. La boucle appelle `handleWrite` :

```cpp
void Client::handleWrite(Epoll & manager)
{
    const char* data_to_send = _response.c_str() + _bytes_send;
    int bytes_left_to_send = _response.length() - _bytes_send;
    int bytes_sent_now = send(this->_fd, data_to_send, bytes_left_to_send, 0);

    if (bytes_sent_now <= 0)
        return ;
    _bytes_send += bytes_sent_now;
    _lastActivity = time(NULL);
    if (_bytes_send == _response.length())
    {
        if (_closeAfterWrite)
        {
            this->closeConnection(manager);
            return ;
        }
        this->cleanResponse();
        this->cleanRequest();
        _ev.events = EPOLLIN;
        manager.mod(this);
        return ;
    }
}
```

*(Server/AroleClass/ClientClass.cpp)*

Le détail qui surprend les débutants : **`send` n'envoie pas forcément tout d'un coup**. Sur un socket non-bloquant, si le tampon d'envoi du système est presque plein, `send` n'écrit qu'une partie et vous rend la main.

D'où la mécanique de `_bytes_send` : c'est un compteur de **« combien d'octets sont déjà partis »**.

- `data_to_send = _response.c_str() + _bytes_send` : on reprend **là où on s'était arrêté**.
- `bytes_left_to_send` : ce qu'il reste à envoyer.
- Après chaque `send` réussi, on avance le compteur.

Tant que tout n'est pas parti, on reste en `EPOLLOUT` et on reviendra ici au prochain tour pour envoyer la suite. Quand `_bytes_send == _response.length()`, tout est envoyé, et `_closeAfterWrite` (décidé à l'acte 5) tranche entre deux destins :

- **`Connection: close`** (ou réponse d'erreur) → `closeConnection`, la conversation est finie.
- **Keep-alive** → on remet la table pour la requête suivante **du même client** : `cleanResponse` (vide la réponse, remet `_bytes_send` à 0) et `cleanRequest` (remet la machine à états à `S_REQUEST_LINE`, `_requestStart` à 0, baisse `_draining`), puis retour en `EPOLLIN`. C'est ça, le keep-alive : la connexion TCP survit à la requête, et un navigateur peut enchaîner dix requêtes sans repayer le coût d'une connexion.

Comme pour `recv`, si `send` renvoie `<= 0`, on ne consulte pas `errno` : on rend la main, on réessaiera.

---

## Acte 7 — La séparation : fermer proprement

Un client s'en va (il a coupé, ou il a eu une erreur, ou il a dépassé son temps). On ferme :

```cpp
void Client::closeConnection(Epoll & manager)
{
    manager.del(this);
    delete this;
}
```

*(Server/AroleClass/ClientClass.cpp)*

Deux gestes, dans cet ordre :

1. **`manager.del(this)`** — on dit à l'`Epoll` d'**arrêter de surveiller** ce FD.
2. **`delete this`** — l'objet se détruit lui-même.

Mais où est le `close()` du FD ? Il n'est **pas ici**, et c'est volontaire. Il est dans le destructeur de la classe de base, appelé automatiquement par le `delete` :

```cpp
Arole::Arole(void) : _fd(-1) {}
Arole::Arole(int fd) : _fd(fd) {}
Arole::~Arole(void) {if (_fd != -1) close(_fd);}
```

*(Server/AroleClass/AroleClass.cpp)*

**Un FD ne doit être fermé qu'une seule fois, à un seul endroit.** Pourquoi ? Si on le fermait à deux endroits, entre les deux le système pourrait réattribuer ce même numéro de FD à une **nouvelle** connexion — qu'on fermerait alors par erreur. En centralisant le `close()` dans `~Arole`, ce bug devient impossible. Le `if (_fd != -1)` protège un dernier cas : un objet qui n'aurait jamais reçu de FD valide ne tentera pas de fermer n'importe quoi (`_fd` vaut `-1` par défaut).

---

## Acte 8 — Le gardien du temps

Reprenons la boucle de l'acte 3. Après avoir traité tous les événements, elle appelle :

```cpp
Arole::launchActiveRoutine(*this);
```

Que fait ce gardien ?

```cpp
void Arole::launchActiveRoutine(Epoll & manager)
{
    time_t now = time(NULL);

    for (std::map<int, std::map<int, Client *> >::iterator it = _activeClient.begin();
         it != _activeClient.end(); ++it)
    {
        std::map<int, Client *>::iterator clientIT = it->second.begin();
        while (clientIT != it->second.end())
        {
            Client *client = clientIT->second;
            clientIT++;
            if (client->getInactiveTime(now))
                client->closeConnection(manager);
        }
    }
}
```

*(Server/AroleClass/AroleClass.cpp)*

Il passe en revue **tous les clients connectés** et ferme ceux qui ont dépassé leur temps :

```cpp
bool Client::getInactiveTime(time_t now) const
{
    if ((now - _lastActivity) >= IDLE_TIMEOUT)
        return (true);
    if (_requestStart != 0)
    {
        if (!_request.headersDone() && (now - _requestStart) >= HEADER_TIMEOUT)
            return (true);
        if ((now - _requestStart) >= REQUEST_TIMEOUT)
            return (true);
    }
    return (false);
}
```

*(Server/AroleClass/ClientClass.cpp)*

Il n'y a pas **un** timeout mais **deux horloges aux rôles opposés** — et confondre les deux ouvre une faille. À expliquer en soutenance :

- **`_lastActivity` + `IDLE_TIMEOUT` (60 s)** : l'horloge d'**inactivité**. Rafraîchie à chaque `recv`/`send` réussi, elle détecte les connexions **mortes ou muettes** (un client parti sans fermer, un keep-alive abandonné).
- **`_requestStart` + deadlines absolues** : l'horloge de la **requête en cours**. Posée au premier octet, **jamais rafraîchie**. C'est la défense **anti-slowloris** : si on se contentait de l'idle timeout, un attaquant enverrait 1 octet toutes les 59 secondes et garderait sa connexion (donc un slot du serveur) ouverte *indéfiniment*, en toute « activité ». Avec la deadline absolue, peu importe qu'il goutte des octets : `HEADER_TIMEOUT` (30 s) le coupe s'il n'a pas fini ses headers (aucun client légitime ne met 30 s à envoyer des headers — c'est précisément la phase qu'attaque le slowloris classique), et `REQUEST_TIMEOUT` (300 s) plafonne la requête entière — assez large pour un vrai upload sur une connexion lente, fatal pour un trickle infini.
- `_requestStart == 0` signifie « pas de requête en cours » (client keep-alive entre deux requêtes) : seul l'idle timeout s'applique alors.

C'est ce duo qui garantit la règle du sujet « une requête ne doit jamais hang indéfiniment » — et c'est pour ça que `epoll_wait` se réveille au moins toutes les 5 s (acte 3) : pour passer ce gardien régulièrement, même sans trafic.

Petit détail élégant : remarquez `clientIT++` **avant** d'éventuellement appeler `closeConnection`. Comme fermer un client le retire de la map (`_activeClient`), on avance l'itérateur **d'abord**, pour ne pas se retrouver avec un itérateur qui pointe sur du vide.

Où est rangée cette liste de clients ? Dans une structure statique partagée, déclarée dans `Arole` :

```cpp
static std::map<int, std::map<int, Client *> > _activeClient;
```

*(Server/AroleClass/AroleClass.hpp)*

Lisez-la comme : « pour chaque socket d'écoute (sa clé = le FD de l'acceptor), la liste de ses clients (clé = le FD du client) ». Ça permet de retrouver, compter, et fermer tous les clients d'un port d'un seul coup.

---

## Acte 9 — Quand on appuie sur Ctrl-C

Dernière scène. Que se passe-t-il si on fait `Ctrl-C` ? Sur la plupart des programmes, ça tue tout. Pas chez nous.

```cpp
volatile sig_atomic_t isRunning = 1;

void ServerManager::handleSigEvent(int signum)
{
    (void)signum;
    isRunning = 0;
}
```

*(Server/ServerManagerClass.cpp)*

Le handler de signal ne fait **qu'une** chose : mettre le drapeau `isRunning` à `0`. (Le type `volatile sig_atomic_t` est le seul qu'on a le droit de modifier en toute sécurité depuis un handler de signal — c'est une règle du langage.)

Souvenez-vous de la boucle (acte 3) :

```cpp
while (this->_active)
{
    if (isRunning)
        { ... boucle d'événements normale ... }
    else
        sigManager();
}
```

Quand `isRunning` tombe à `0`, on ne tue pas le serveur : on bascule dans `sigManager`, un **menu interactif** (`SHOW` / `START` / `STOP` / `EXIT`) qui permet d'inspecter le serveur, d'ouvrir ou fermer des ports à chaud. `EXIT` met `_active` à `0` pour sortir vraiment ; reprendre l'exécution remet `isRunning` à `1` et on repart dans la boucle normale.

---

## Épilogue — Ce qui viendra se poser sur cette fondation

Tout ce qu'on vient de raconter est **le moteur plus l'oreille et la voix** du serveur. Les prochaines features se branchent dessus aux endroits laissés ouverts.

- **Le parsing HTTP** ✅ Fait : la machine à états `HttpRequest` vit dans `Http/`, nourrie par `Client::handleRead` (acte 5). Chunked dé-chunké, keep-alive géré, `413` anticipé avec drain.
- **La réponse HTTP** ✅ Fait (le contenant) : `HttpResponse` assemble status line + headers + body, envoyé par `handleWrite` avec sa mécanique d'envoi partiel inchangée.
- **Le routing** ✅ Fait (statique) : `Client::genResponse` ne fabrique plus un récap de debug — il choisit le virtual host (`selectVhost`) puis délègue au **`Router`** (`Router/`), qui matche la `location` au préfixe le plus long, applique `root` (mapping *alias*, préfixe retiré), résout `index`, vérifie les `methods` (→ `405` + `Allow`), bloque le path traversal, et sert le fichier en GET (`stat`/`open`/`read` + MIME) ou redirige (`return` 30x). Détail complet dans **`docs/ROUTING.md`**. Restent à brancher, aux points d'insertion déjà prévus dans `run()` : l'autoindex, les `error_page` custom, POST/upload, DELETE.
- **Le CGI** sera... **un nouveau `Arole`**. Les tuyaux (`pipe`) d'un programme CGI sont des FD comme les autres : on créera une classe `CgiPipe : public Arole`, on l'enregistrera avec `manager.add(...)`, et le même `epoll` la surveillera. C'est exactement ce que l'abstraction `Arole` a été conçue pour permettre.
- **Le fichier de configuration** est le point d'entrée et construit les `Server`/`Socket` — c'est l'objet de l'**acte 0**. ✅ Fait.

Si vous avez compris les neuf actes, vous avez compris le serveur. Le reste, ce sont des invités qui arrivent à une fête déjà bien organisée.

---

### Pour réviser en 30 secondes

0. La **config** est lue en premier (acte 0) : lexer → descente récursive → des `ServerConfig`, avec héritage `server`→`location` et virtual hosts sur un même `host:port`.
1. Un seul `Epoll`, un seul `epoll_wait` : c'est le seul endroit où on attend.
2. Tout ce qu'on surveille est un `Arole` ; on retrouve le bon objet via `data.ptr` ; on appelle `handle_event` — le polymorphisme fait le tri.
3. On réagit à **ce qui est prêt** (`events`), jamais à **ce qui nous intéresse** (`_ev.events`).
4. Jamais d'`errno` après `recv`/`send` ; les vraies erreurs reviennent par `EPOLLERR`/`EPOLLHUP`.
5. `send` peut être partiel → on suit `_bytes_send`.
6. On bascule l'intérêt `EPOLLIN` ↔ `EPOLLOUT` avec `manager.mod(...)` — et les **deux à la fois** pour drainer une erreur (`413`) sans deadlock.
7. Un FD se ferme **une fois**, dans `~Arole`. Après tout appel qui peut fermer (`closeConnection`, `add/mod/del`), on `return` toujours.
8. La requête est comprise par une **machine à états** (`HttpRequest`) : incrémentale, fail-fast, chunked dé-chunké, `413` vérifié pendant la lecture.
9. Deux horloges : **idle** (60 s, rafraîchie) pour les connexions mortes, **deadline absolue** (30 s headers / 300 s requête, jamais rafraîchie) contre le slowloris.
