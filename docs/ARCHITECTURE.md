# Architecture du serveur — le cœur (Reactor pattern)

> Document de référence pour comprendre **comment le serveur fonctionne en interne**, avant de réviser pour l'évaluation.
> Il décrit la boucle événementielle (le « moteur »). Les features HTTP (parsing, méthodes, CGI…) viennent **par-dessus** ce moteur, sans le modifier.

---

## 1. Le problème à résoudre

Le sujet impose des contraintes très précises (cf. `CLAUDE.md` §0) :

- **Un seul** `poll()`/`epoll`/`select`/`kqueue` pour **toutes** les I/O (sockets d'écoute + clients).
- **Jamais** de `read`/`recv`/`write`/`send` sur un socket sans que le multiplexeur ait dit « ce FD est prêt ».
- Tous les FD socket en **non-bloquant**.
- **Interdit de regarder `errno`** après un `read`/`write`.
- Le serveur ne doit **jamais** hang, et rester dispo même sous charge.

La réponse classique à ce genre de contrainte est un **Reactor pattern**.

---

## 2. Le Reactor pattern en une phrase

> Un **seul** thread, **un seul** point d'attente (`epoll_wait`). On dort jusqu'à ce qu'un ou plusieurs FD soient « prêts », puis pour chaque FD prêt on appelle **le bon gestionnaire d'événement** (handler). Pas de blocage, pas de threads, pas de busy-loop.

Les trois rôles du pattern, et leur incarnation chez nous :

| Rôle du pattern | Notre classe | Fichier |
|---|---|---|
| **Reactor / Dispatcher** (attend les events et les distribue) | `Epoll` | `Epoll/epollClass.{hpp,cpp}` |
| **EventHandler** (interface abstraite « je sais traiter mon event ») | `Arole` | `Server/AroleClass/AroleClass.*` |
| **Handler concret** : accepte les connexions | `Acceptor` | `Server/AroleClass/AcceptorClass.cpp` |
| **Handler concret** : gère un client connecté | `Client` | `Server/AroleClass/ClientClass.cpp` |

Autour, des classes « support » :

| Classe | Rôle | Fichier |
|---|---|---|
| `Socket` | Crée/configure un socket d'écoute (`socket`/`bind`/`listen`) et possède son `Acceptor` | `Server/socket/socketClass.cpp` |
| `Sockaddr` | Wrapper C++ autour de `struct sockaddr_in` | `Server/socket/sockaddrClass.cpp` |
| `Server` | Regroupe plusieurs `Socket` (multi-port) | `Server/ServerClass.cpp` |

---

## 3. L'idée clé : « polymorphisme via `data.ptr` »

Quand on enregistre un FD dans epoll, on remplit un `struct epoll_event` :

```cpp
_ev.events   = EPOLLIN;   // ce qu'on veut surveiller
_ev.data.ptr = this;      // ← on stocke le pointeur vers le handler (Arole*)
```

epoll **nous redonne ce pointeur** quand le FD devient prêt. Donc, dans la boucle, on n'a pas besoin de savoir si un FD est un socket d'écoute ou un client : on récupère le `Arole*` et on appelle sa méthode virtuelle :

```cpp
Arole *role = static_cast<Arole *>(_events[i].data.ptr);
role->handle_event(*this, _events[i].events);   // dispatch O(1), polymorphe
```

`Acceptor` et `Client` redéfinissent chacun `handle_event`. **C'est tout le secret du dispatch.** Ajouter un nouveau type de FD (ex : un pipe de CGI) = créer une nouvelle sous-classe de `Arole`, sans toucher à `Epoll`.

---

## 4. La boucle événementielle (`Epoll::run`)

```
while (_active)                      // _active = le serveur tourne
  if (isRunning)                     // isRunning = pas en pause (cf. signaux §8)
      n = epoll_wait(...)            // ← LE SEUL point d'attente de tout le programme
      pour chaque FD prêt i :
          role = _events[i].data.ptr
          role->handle_event(*this, _events[i].events)
      Arole::launchActiveRoutine()   // balaie les clients en timeout
      show()                         // debug
  else
      sigManager()                   // menu interactif quand on a fait Ctrl-C
```

- `epoll_wait` a un **timeout de 60 s** : même sans activité réseau, on se réveille pour vérifier les timeouts clients → le serveur ne reste jamais bloqué indéfiniment.
- `EINTR` (réveil par un signal) est géré dans `Epoll::wait` : on retourne `0` au lieu de planter.

---

## 5. Distinction CRUCIALE : masque d'intérêt vs events prêts

C'est **le** point que l'évaluation peut tester, et la source d'erreur la plus fréquente. Il y a **deux** champs `.events` différents :

| | Qui le remplit | Signification | Où |
|---|---|---|---|
| **Masque d'intérêt** | **nous** | « préviens-moi pour EPOLLIN » | `_ev.events` (membre de notre objet) |
| **Events prêts** | **le noyau** | « voilà ce qui vient de se produire » | `_events[i].events` (rempli par `epoll_wait`) |

➡️ **Un handler doit toujours réagir à ce qui est PRÊT (`_events[i].events`), jamais à ce qui l'INTÉRESSE (`_ev.events`).**

C'est pour ça que `handle_event` reçoit `uint32_t events` en paramètre : on lui descend le masque prêt depuis la boucle. Et le noyau peut nous rendre des bits qu'on n'a jamais demandés : **`EPOLLERR` et `EPOLLHUP`** (erreur / déconnexion). On les teste **en premier**.

```cpp
void Client::handle_event(Epoll & manager, uint32_t events)
{
    if (events & (EPOLLERR | EPOLLHUP)) { closeConnection(manager); return ; }
    if (events & EPOLLIN)  { if (!handleRead(manager)) return ; }
    if (events & EPOLLOUT) { handleWrite(manager); }
}
```

> Deux `if` séparés (pas `else if`) : `EPOLLIN` et `EPOLLOUT` peuvent être prêts **en même temps**. Le sujet exige justement de surveiller lecture **et** écriture simultanément.

---

## 6. Pourquoi on ne regarde JAMAIS `errno` après recv/send

Le sujet l'interdit. Comme on n'agit que sur un FD que epoll a déjà déclaré prêt, on n'a pas besoin d'errno :

- `recv` **> 0** → on a des données, on les ajoute au buffer.
- `recv` **== 0** → le client a fermé proprement (c'est une **valeur de retour**, pas errno → autorisé) → on ferme.
- `recv` **< 0** → on **ne lit pas errno**, on `return` simplement. On retentera au prochain tour.
- `send` **<= 0** → idem, `return`.

Et si une vraie erreur réseau survient (reset, etc.) ? Le noyau nous la signalera via **`EPOLLERR`/`EPOLLHUP`**, qu'on traite en haut de `handle_event`. C'est ce mécanisme qui **remplace** la lecture d'errno.

---

## 7. Cycle de vie d'une connexion (du début à la fin)

```
                 ┌─────────────────────────────────────────────┐
   Client TCP ──▶│ Socket d'écoute (Acceptor) prêt en lecture   │
                 └───────────────────┬─────────────────────────┘
                                     │ epoll_wait rend EPOLLIN sur l'acceptor
                                     ▼
                Acceptor::handle_event
                   ├─ accept()                → nouveau fd client
                   ├─ new Client(fd, servfd)  → enregistré dans _activeClient
                   ├─ fcntl(fd, O_NONBLOCK)
                   └─ manager.add(client)     → epoll surveille le client (EPOLLIN)
                                     │
        ┌────────────────────────────┴───────────────────────────┐
        ▼ (epoll_wait rend EPOLLIN sur le client)                 │
   Client::handleRead                                             │
     ├─ recv() → append dans _request                            │
     └─ requête complète ? → genResponse()                       │
                            → _ev.events = EPOLLOUT ; mod()       │
        ▼ (epoll_wait rend EPOLLOUT)                              │
   Client::handleWrite                                            │
     ├─ send() le _response (envoi PARTIEL suivi via _bytes_send)│
     └─ tout envoyé ? → _ev.events = EPOLLIN ; mod()  ───────────┘ (keep-alive : on reboucle)
        ▼
   Fermeture (recv==0, EPOLLHUP, ou timeout)
   Client::closeConnection → manager.del(this) ; delete this
```

Points à retenir :

- **Envoi partiel** : un `send` non-bloquant peut n'écrire qu'une partie. On mémorise combien d'octets sont déjà partis (`_bytes_send`) et on reprend là où on s'était arrêté au prochain `EPOLLOUT`. **Ne jamais supposer que tout part d'un coup.**
- **Toggle** : tant qu'on lit, on surveille `EPOLLIN` ; quand la réponse est prête, on bascule en `EPOLLOUT` ; une fois envoyée, on rebascule en `EPOLLIN`. (Le cas où on surveille les deux en même temps — anti-deadlock upload — viendra avec `client_max_body_size`.)

---

## 8. Gestion mémoire et fermeture des FD (règles dures)

Deux pièges classiques en Reactor, qu'on a verrouillés :

**a) Un FD ne se ferme qu'à UN seul endroit.**
Le `close()` est **uniquement** dans `Arole::~Arole`, gardé par `if (_fd != -1)`. Aucun autre code n'appelle `close()` sur ces FD. Sinon on risque un **double-close** : entre les deux fermetures, le noyau peut réattribuer le numéro de FD à une autre connexion qu'on fermerait par erreur.

**b) Invariant `delete this`.**
`Client::closeConnection` fait `delete this` (l'objet se suicide). Donc **tout appel qui peut fermer la connexion** (`closeConnection`, `manager.add/mod/del`, `handleRead` renvoyant `false`) **doit être immédiatement suivi d'un `return`**. Toucher `this` après = use-after-free.

Chaîne de possession (qui détruit qui) :

```
Epoll  ──possède──▶ Server*  ──possède──▶ Socket*  ──possède (par valeur)──▶ Acceptor
                                                                               │
                                          _activeClient (static) ◀── Client* ──┘ (suivi par acceptor)
```

- À l'arrêt, `~Epoll` → `delete` les `Server` → `~Server` → `delete` les `Socket` → `~Socket` → `Acceptor::closeAll` ferme tous les `Client`, puis `~Acceptor`/`~Arole` ferme le FD d'écoute.
- `Acceptor::closeAll` **copie** la map de clients avant d'itérer, car chaque `Client::~Client` modifie la vraie map (`_activeClient`) — éviter d'invalider l'itérateur.

**Suivi des clients** : `Arole::_activeClient` est une `map<acceptorFd, map<clientFd, Client*>>` **statique** (partagée). Elle sert à (1) compter les clients par socket, (2) tous les fermer quand un socket ferme, (3) balayer les timeouts.

---

## 9. Timeouts (ne jamais hang)

`Arole::launchActiveRoutine`, appelée à chaque tour de boucle, parcourt tous les clients : si `now - _lastActivity >= 60 s`, le client est fermé. `_lastActivity` est rafraîchi à chaque réponse envoyée. Combiné au timeout de 60 s de `epoll_wait`, ça garantit qu'une connexion morte/silencieuse finit toujours par être nettoyée.

---

## 10. Multi-port et signaux

- **Multi-port** : `Server` tient une `map<port, Socket*>`. Chaque `Socket` = un `socket`/`bind`/`listen` + son `Acceptor`. On peut ouvrir/fermer des ports à chaud (`start`/`stop`).
- **Signaux** : `SIGINT` (Ctrl-C) ne tue **pas** le serveur. Le handler met le flag global `isRunning = 0`. La boucle bascule alors dans `sigManager`, un **menu interactif** (SHOW / START / STOP / EXIT) qui permet d'inspecter/piloter le serveur. `EXIT` met `_active = 0` (arrêt propre), `exit` reprend l'exécution normale.
  - `isRunning` est un `volatile sig_atomic_t` : c'est le **seul** type sûr à modifier depuis un handler de signal.

---

## 11. Où se brancheront les features (pour la suite)

Tout vient **par-dessus** ce moteur, sans le casser :

- **Parsing HTTP** → une **machine à états dans `Client`** (READING_HEADERS → READING_BODY → READY), alimentée par les `recv` partiels déjà gérés dans `handleRead`.
- **Génération de réponse** → remplace le placeholder `genResponse` (status line + headers + body), toujours envoyée via `handleWrite`.
- **CGI** → une **nouvelle sous-classe de `Arole`** (`CgiPipe`) : les pipes du CGI sont des FD comme les autres, surveillés par le même `epoll`. C'est précisément ce que l'abstraction `Arole` permet.
- **Config** → un parseur qui devient le point d'entrée et construit les `Server`/`Socket`.

---

## 12. Glossaire express

- **Non-bloquant** : un FD configuré avec `O_NONBLOCK` ; `recv`/`send` retournent immédiatement au lieu d'attendre.
- **Level-triggered** (notre mode epoll par défaut) : tant qu'il reste des données à lire, epoll continue de nous le signaler → un `accept`/`recv` par event suffit. (À l'inverse, *edge-triggered* `EPOLLET` exigerait de tout vider en boucle.)
- **`EPOLLIN`** : prêt en lecture. **`EPOLLOUT`** : prêt en écriture. **`EPOLLERR`/`EPOLLHUP`** : erreur / pair déconnecté.
- **Handler** : objet (`Acceptor`/`Client`) qui sait traiter l'event de son propre FD.

---

## 13. Carte des fichiers (pour réviser vite)

```
main.cpp                          point d'entrée : crée Epoll + Server, lance run()
Epoll/epollClass.cpp              le Reactor : epoll_wait + dispatch + boucle + signaux
Server/ServerClass.cpp            regroupe les sockets (multi-port)
Server/socket/socketClass.cpp     un socket d'écoute (socket/bind/listen)
Server/socket/sockaddrClass.cpp   wrapper sockaddr_in
Server/AroleClass/AroleClass.cpp  base abstraite + suivi clients + timeouts
Server/AroleClass/AcceptorClass.cpp   accepte les connexions
Server/AroleClass/ClientClass.cpp     lit/écrit/ferme un client (handleRead/handleWrite)
```

Bonne révision. En cas de doute sur une contrainte : comparer avec **NGINX** et relire `CLAUDE.md` §0.
