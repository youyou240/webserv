# Le cœur du serveur — l'histoire d'une requête

> Ce document raconte, pas à pas, **ce qui se passe vraiment** quand notre serveur démarre et qu'un client s'y connecte.
> Pas besoin de connaissances préalables : on suit le code dans l'ordre où il s'exécute, on cite à chaque fois le fichier où ça se passe, et on s'arrête sur les passages importants.
> Tout ce qui est décrit ici est **la fondation** du serveur : ça ne bougera plus. Les features HTTP viendront se poser dessus, on en parle à la fin.

---

## Les personnages de l'histoire

Avant de lever le rideau, présentons les acteurs. Ils sont peu nombreux, et c'est voulu.

- **`Epoll`** *(Epoll/epollClass.cpp)* — le chef d'orchestre. Il y en a **un seul** dans tout le programme. C'est lui qui attend que quelque chose se passe et qui prévient le bon acteur.
- **`Arole`** *(Server/AroleClass/AroleClass.cpp)* — une **classe abstraite**. Elle dit : « tout objet surveillé par `Epoll` doit savoir réagir à son propre événement ». Elle ne sait pas *comment*, elle impose juste le contrat.
- **`Acceptor`** *(Server/AroleClass/AcceptorClass.cpp)* — un `Arole` concret. Sa seule mission : **accepter les nouvelles connexions** sur un port.
- **`Client`** *(Server/AroleClass/ClientClass.cpp)* — un autre `Arole` concret. Il représente **un visiteur connecté** : il lit ce qu'on lui envoie, il répond, puis il s'en va.
- **`Socket`** *(Server/socket/socketClass.cpp)* et **`Server`** *(Server/ServerClass.cpp)* — la logistique : ouvrir les ports, en gérer plusieurs.

Retenez surtout cette phrase, tout le reste en découle :

> **`Acceptor` et `Client` sont tous les deux des `Arole`.** Donc `Epoll` peut les traiter de la même manière, sans savoir lequel est lequel.

---

## Acte 1 — Le serveur s'éveille

Tout commence dans `main.cpp`. Quatre lignes suffisent à donner vie au serveur :

```cpp
Epoll & epoll = Epoll::createEpoll();
Server *serv = new Server(nbPort, ports, epoll);
std::signal(SIGINT, epoll.handleSigEvent);
epoll.addServer(serv);
epoll.run();
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
Epoll::Epoll(void) : _events(MIN_MAX), _active(1)
{
    _fd = epoll_create(1);
    if (_fd == -1)
        throw epollError();
}
```

*(Epoll/epollClass.cpp)*

`epoll_create` renvoie un **descripteur de fichier** (`_fd`) qui représente « la liste des choses que je surveille ». `_events` est un tableau qui servira de boîte aux lettres : c'est là que le noyau déposera les événements quand il y en aura.

### `new Server(...)` — ouvrir les portes

Le `Server` crée les sockets d'écoute. Pour chaque port demandé, un `Socket` est construit, et c'est dans son constructeur que se joue toute la mécanique réseau classique :

```cpp
Socket::Socket(unsigned short int port)
{
    _sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (_sockfd == -1)
        throw badExec();
    if (fcntl(_sockfd, F_SETFL, O_NONBLOCK) == -1)
        throw badExec();
    _addr.setPort(port);
    if (bind(_sockfd, (struct sockaddr *)&_addr.getStruct(), sizeof(struct sockaddr)) == -1)
        throw badExec();
    _acceptor.setFD(_sockfd);
    if (listen(_sockfd, SOMAXCONN) == -1)
    {
        close(_sockfd);
        throw badExec();
    }
}
```

*(Server/socket/socketClass.cpp)*

C'est la séquence fondatrice de tout serveur réseau :

1. **`socket()`** — « donne-moi une prise réseau ». On récupère un FD.
2. **`fcntl(..., O_NONBLOCK)`** — on la rend **non-bloquante**. C'est capital : sans ça, un `recv` pourrait figer tout le serveur en attendant des données. Avec ça, les appels rendent la main immédiatement.
3. **`bind()`** — « cette prise écoutera sur ce port ».
4. **`listen()`** — « accepte les connexions entrantes ». Le port est maintenant ouvert.

Notez `_acceptor.setFD(_sockfd)` : le `Socket` confie le FD d'écoute à son `Acceptor`, qui sera chargé de tout ce qui arrive dessus.

### `epoll.run()` — entrer dans la danse

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
void Epoll::run(void)
{
    std::cout << *this << std::endl;
    while (this->_active)
    {
        if (isRunning)
        {
            int num_events = wait();
            for (int i = 0; i < num_events; ++i)
            {
                Arole *role = static_cast<Arole *>(_events[i].data.ptr);
                if (role == NULL)
                    continue ;
                role->handle_event(*this, _events[i].events);
            }
            Arole::launchActiveRoutine(*this);
            show();
        }
        else
            sigManager();
    }
}
```

*(Epoll/epollClass.cpp)*

Décortiquons le moment le plus important : `wait()`.

```cpp
int Epoll::wait(void)
{
    int events = epoll_wait(_fd, _events.data(), _events.size(), 60000);
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

`epoll_wait` est **le seul endroit où le programme dort**. Il dit au noyau : « réveille-moi seulement quand au moins un de mes FD surveillés a quelque chose à faire — ou au bout de 60 secondes (60000 ms), même s'il ne se passe rien ». Tant que le serveur est inactif, il ne consomme **aucun** CPU : il attend.

Pourquoi ce timeout de 60 s alors qu'on pourrait attendre indéfiniment ? Parce qu'on veut **périodiquement vérifier les connexions trop vieilles** (on y revient à l'acte 6). Le serveur ne reste donc jamais bloqué pour toujours, même sans trafic.

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
    Client *client = new Client(fd, this->_fd);
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
Client::Client(int fd, int servfd)
    : Arole(fd), _acceptorfd(servfd), _bytes_send(0),
      _complete(false), _lastActivity(time(NULL))
{
    this->_client += 1;
    _activeClient[_acceptorfd].insert(std::pair<int, Client *>(_fd, this));
    _ev.events = EPOLLIN;
    _ev.data.ptr = this;
}
```

*(Server/AroleClass/ClientClass.cpp)*

`_ev.events = EPOLLIN` : pour l'instant, ce client nous intéresse **en lecture** (on attend qu'il nous parle). `_ev.data.ptr = this` : encore notre carte d'identité. Et `_lastActivity = time(NULL)` enregistre l'heure de naissance — on s'en servira pour les timeouts.

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
        this->closeConnection(manager);
        return (false);
    }
    if (bytes_read < 0)
        return (true);
    _request.append(buffer, bytes_read);
    ...
    if (_complete)
    {
        this->genResponse();
        _ev.events = EPOLLOUT;
        manager.mod(this);
    }
    return (true);
}
```

*(Server/AroleClass/ClientClass.cpp)*

Trois cas après `recv`, et **on ne regarde jamais `errno`** (le sujet l'interdit après une lecture/écriture) :

- **`bytes_read == 0`** : le client a fermé proprement de son côté. (C'est une **valeur de retour**, pas `errno` : autorisé.) → on ferme, et on renvoie `false` pour signaler « ce client n'existe plus, n'y touche pas ».
- **`bytes_read < 0`** : pas de données disponibles pour l'instant. On ne s'affole pas, on ne ferme pas : on renvoie simplement la main, on réessaiera quand le noyau nous re-préviendra. Et si c'était une vraie erreur réseau ? Elle reviendra sous forme de `EPOLLERR`/`EPOLLHUP`, traités en haut de `handle_event`. C'est ce mécanisme qui **remplace** la lecture d'`errno`.
- **`bytes_read > 0`** : on accumule les octets dans `_request`. On accumule, car une requête peut arriver en plusieurs morceaux.

Quand la requête est **complète** (`_complete`), on prépare la réponse, et surtout : on **bascule l'intérêt** vers `EPOLLOUT` (« maintenant je veux écrire ») puis on prévient l'`Epoll` avec `manager.mod(this)`. La prochaine fois, c'est en écriture que le client sera prêt.

> Le `return (false)` après une fermeture n'est pas un détail : dès qu'une connexion est fermée, l'objet `Client` est **détruit** (acte 6). Le `false` permet à `handle_event` de s'arrêter net et de ne plus jamais toucher l'objet. **Règle d'or : tout ce qui peut fermer la connexion est suivi d'un `return`.**

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
    if (_bytes_send == _response.length())
    {
        this->cleanResponse();
        this->_lastActivity = time(NULL);
        _ev.events = EPOLLIN;
        manager.mod(this);
    }
}
```

*(Server/AroleClass/ClientClass.cpp)*

Le détail qui surprend les débutants : **`send` n'envoie pas forcément tout d'un coup**. Sur un socket non-bloquant, si le tampon d'envoi du système est presque plein, `send` n'écrit qu'une partie et vous rend la main.

D'où la mécanique de `_bytes_send` : c'est un compteur de **« combien d'octets sont déjà partis »**.

- `data_to_send = _response.c_str() + _bytes_send` : on reprend **là où on s'était arrêté**.
- `bytes_left_to_send` : ce qu'il reste à envoyer.
- Après chaque `send` réussi, on avance le compteur.

Tant que tout n'est pas parti, on reste en `EPOLLOUT` et on reviendra ici au prochain tour pour envoyer la suite. Quand `_bytes_send == _response.length()`, tout est envoyé : on nettoie, on remet l'heure de dernière activité à jour, et on **rebascule l'intérêt vers `EPOLLIN`** — prêt pour une nouvelle requête du même client.

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

Il passe en revue **tous les clients connectés** et ferme ceux qui dorment depuis trop longtemps :

```cpp
bool Client::getInactiveTime(time_t now) const
{
    if ((now - _lastActivity) >= 60)
        return true;
    return false;
}
```

*(Server/AroleClass/ClientClass.cpp)*

Un client qui n'a rien fait depuis 60 secondes est considéré mort, et on le ferme. C'est ce qui empêche qu'une connexion ouverte mais silencieuse ne reste là pour toujours (et c'est pour ça que `epoll_wait` se réveille au moins toutes les 60 s — pour passer ce gardien régulièrement, même sans trafic).

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

void Epoll::handleSigEvent(int signum)
{
    (void)signum;
    isRunning = 0;
}
```

*(Epoll/epollClass.cpp)*

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

Tout ce qu'on vient de raconter est **le moteur**, et il est terminé. Les features HTTP ne le modifieront pas : elles se brancheront dessus aux endroits qu'on a laissés ouverts.

- **Le parsing HTTP** prendra place dans `Client::handleRead` : c'est là qu'on accumule déjà les octets dans `_request`. On y mettra une petite machine à états qui décidera *quand* la requête est vraiment complète (ligne de requête, en-têtes, corps), au lieu d'un simple drapeau.
- **La vraie réponse HTTP** remplacera la génération actuelle, mais partira toujours par `handleWrite`, avec sa mécanique d'envoi partiel inchangée.
- **Le CGI** sera... **un nouveau `Arole`**. Les tuyaux (`pipe`) d'un programme CGI sont des FD comme les autres : on créera une classe `CgiPipe : public Arole`, on l'enregistrera avec `manager.add(...)`, et le même `epoll` la surveillera. C'est exactement ce que l'abstraction `Arole` a été conçue pour permettre.
- **Le fichier de configuration** deviendra le point d'entrée et construira les `Server`/`Socket` à la place des arguments actuels.

Si vous avez compris les neuf actes, vous avez compris le serveur. Le reste, ce sont des invités qui arrivent à une fête déjà bien organisée.

---

### Pour réviser en 30 secondes

1. Un seul `Epoll`, un seul `epoll_wait` : c'est le seul endroit où on attend.
2. Tout ce qu'on surveille est un `Arole` ; on retrouve le bon objet via `data.ptr` ; on appelle `handle_event` — le polymorphisme fait le tri.
3. On réagit à **ce qui est prêt** (`events`), jamais à **ce qui nous intéresse** (`_ev.events`).
4. Jamais d'`errno` après `recv`/`send` ; les vraies erreurs reviennent par `EPOLLERR`/`EPOLLHUP`.
5. `send` peut être partiel → on suit `_bytes_send`.
6. On bascule l'intérêt `EPOLLIN` ↔ `EPOLLOUT` avec `manager.mod(...)`.
7. Un FD se ferme **une fois**, dans `~Arole`. Après une fermeture, on `return` toujours.
8. Un gardien ferme les clients inactifs depuis 60 s.
