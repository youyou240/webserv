# Le CGI — exécuter un programme pour fabriquer la réponse

> Jusqu'ici, toute réponse était fabriquée **par nous**, de façon **synchrone** (le `Router` lit un fichier et rend une `HttpResponse` immédiatement). Le CGI change tout : la réponse est produite **par un autre programme** (un script), au fil du temps, lu sur un tuyau. Ce document explique tout, de la théorie au code réel.
>
> Ce document couvre le CGI **complet : GET et POST** (le POST écrit le corps de la requête sur l'entrée `stdin` du script).

---

## 1. Qu'est-ce qu'un CGI ? (le contrat, pas un langage)

**CGI = Common Gateway Interface.** Ce n'est ni Python, ni PHP : c'est un **contrat** entre le serveur et **n'importe quel programme exécutable**. Le programme (le « script CGI ») doit :

1. **lire les infos de la requête** dans des **variables d'environnement** que le serveur lui fournit (`REQUEST_METHOD`, `QUERY_STRING`, `CONTENT_LENGTH`…) ;
2. **lire le corps** de la requête sur son **entrée standard** (`stdin`) — pour un POST ;
3. **écrire** sa réponse sur sa **sortie standard** (`stdout`) : des en-têtes, une ligne vide, puis le corps.

Le serveur **capture ce `stdout`** et le transforme en réponse HTTP. **Le langage du script n'a aucune importance** : C, Python, PHP, bash… tant qu'il respecte ce contrat.

### Un CGI en C, pour comprendre

```c
#include <stdio.h>
#include <stdlib.h>

int main(void)
{
    char *method = getenv("REQUEST_METHOD");   // le serveur nous l'a mis dans l'env
    printf("Content-Type: text/plain\r\n");    // un en-tete
    printf("\r\n");                            // ligne vide = fin des en-tetes
    printf("Hello from a C CGI!\n");           // le corps
    printf("REQUEST_METHOD = %s\n", method ? method : "(unset)");
    return (0);
}
```

C'est *tout* un CGI. On fournit `www/cgi-bin/hello.c` (compilé), `hello.py`, `hello.php` — trois langages, **un seul mécanisme**.

---

## 2. Interprété vs compilé : la seule vraie différence

Le serveur lance le script avec `execve`. Deux cas :

- **Interprété** (`.py`, `.php`) : le fichier n'est pas exécutable seul, il faut un **interpréteur** :
  `execve("/usr/bin/python3", ["/usr/bin/python3", "hello.py"], env)`.
  D'où la config `cgi .py /usr/bin/python3` (extension → interpréteur).

- **Compilé** (`.cgi`, un binaire C) : le binaire **est** le programme, **pas d'interpréteur** :
  `execve("./hello.cgi", ["./hello.cgi"], env)`.
  D'où la config `cgi .cgi` **sans** interpréteur.

Notre moteur gère **les deux** : dans `configs/default.conf`, la directive `cgi` accepte désormais un interpréteur **optionnel** (parser assoupli). La location `/cgi-bin` déclare les trois :

```
location /cgi-bin {
    methods GET POST;
    root ./www/cgi-bin;
    cgi .cgi;                    # C : binaire direct
    cgi .py  /usr/bin/python3;   # Python : interpreté
    cgi .php /usr/bin/php-cgi;   # PHP : interpreté
}
```

> **PHP** : `php-cgi` n'est **pas** installé par défaut dans le conteneur (à `apt install php-cgi` si tu veux le tester). Il a une particularité : il refuse de tourner sans la variable `REDIRECT_STATUS` (une sécurité) — on la fournit (`REDIRECT_STATUS=200`).

---

## 3. Pourquoi le CGI casse le modèle synchrone

Rappel du flux normal (cf. `ARCHITECTURE.md` acte 5) : requête complète → `genResponse()` construit **immédiatement** `_response` → on envoie.

Le CGI ne peut **pas** être synchrone :
1. il faut **forker** un processus (le script) et lui parler par des **pipes** ;
2. ces pipes sont de **nouveaux descripteurs** qui doivent passer par l'**unique `epoll`**, en **non-bloquant** (sinon note 0) ;
3. la réponse n'existe qu'**après** avoir lu le `stdout` du script jusqu'à **EOF**.

C'est exactement ce que l'abstraction `Arole` permet : **tout FD surveillé est un `Arole`**. Le pipe du CGI devient un nouvel acteur du même `epoll`.

---

## 4. L'architecture : coordinateur `Cgi` + acteur `CgiOut`

Deux classes, dans `Cgi/CgiClass.{hpp,cpp}` :

- **`Cgi`** (le **coordinateur**, possédé par le `Client`) : tient le `pid` de l'enfant, la sortie accumulée, l'heure de départ, et un lien vers le `Client`. Il **fork** le script et **fabrique** la réponse finale.
- **`CgiOut : public Arole`** : enveloppe le **bout de lecture du pipe `stdout`**. C'est un `Arole` (un FD = un acteur) ; quand l'`epoll` dit « ce pipe a des données », son `handle_event` lit un morceau.

```
        Client  (parqué pendant le CGI)
          │ possède
          ▼
        Cgi  ── fork ──►  processus enfant (le script)
          │ possède              │ écrit sur stdout
          ▼                      ▼
       CgiOut  ◄──── pipe ──── (stdout du script)
          │ (dans l'epoll unique)
```

En **GET** (pas de corps), il n'y a qu'un pipe utile (le `stdout`) : le `stdin` du script est fermé tout de suite (EOF immédiat). En **POST**, un second acteur **`CgiIn : public Arole`** enveloppe le bout d'écriture du `stdin` : sur `EPOLLOUT`, il écrit le corps de la requête (déjà dé-chunké par le parseur) morceau par morceau ; quand tout est écrit, il **ferme `stdin`** (le script reçoit EOF et peut finir). **Les deux pipes sont dans l'epoll en même temps** → aucun *deadlock* possible (on n'attend jamais l'un en bloquant sur l'autre).

---

## 5. Le cycle de vie, étape par étape (GET)

### a) Détection & résolution — `Router::resolveCgi`
À requête complète, `Client::tryStartCgi` demande au `Router` : « ce chemin est-il un CGI ? ». `resolveCgi` matche la `location`, mappe l'URL vers le fichier disque (mapping *alias*, cf. `ROUTING.md`), regarde si l'**extension** est dans la map `cgi`. Si oui → renvoie le **chemin du script** + l'**interpréteur** (vide pour le C). Si le script n'existe pas → `404`.

### b) Spawn — `Cgi::start`
```
pipe(outPipe)                     ← tuyau stdout : [0]=parent lit, [1]=enfant écrit
pipe(inPipe)                      ← tuyau stdin (fermé tout de suite en GET)
fork()
  ── enfant ──
    dup2(inPipe[0],  STDIN)       ← le script lira son stdin ici
    dup2(outPipe[1], STDOUT)      ← ce que le script imprime part dans le tuyau
    close(tous les bouts de pipe)
    chdir(dossier du script)      ← pour que les chemins relatifs du script marchent
    execve(interpréteur ou binaire, argv, env)
    _exit(1)                      ← seulement si execve échoue
  ── parent ──
    close(inPipe[0], outPipe[1], inPipe[1])   ← on ferme ce qu'on n'utilise pas ; fermer inPipe[1] => stdin du script = EOF
    fcntl(outPipe[0], O_NONBLOCK)             ← non-bloquant (obligatoire)
    fcntl(outPipe[0], FD_CLOEXEC)             ← voir §7 (CGI concurrents)
    new CgiOut(outPipe[0]) ; epoll.add(...)   ← le pipe entre dans l'epoll unique
```

### c) Lecture du `stdout` — `CgiOut::handle_event`
L'`epoll` réveille `CgiOut` quand le pipe a des données. On `read` un morceau, on l'ajoute à `_output`, et on **rend la main** (jamais bloquant, comme pour un socket). Quand `read` renvoie **0**, c'est l'**EOF** : le script a fini d'écrire (il a fermé `stdout`, généralement en se terminant).

### d) Fin — `Cgi::onEof`
Sur EOF : on **récupère** le processus (`waitpid`, non-bloquant), on **parse** la sortie du script (§6), on **construit** la réponse HTTP, on la **remet au `Client`** (qui bascule en écriture et l'envoie), puis on **nettoie** (ferme le pipe, se détruit).

---

## 6. Parser la sortie du CGI — `Cgi::buildResponse`

Le script écrit un mini-message façon HTTP : des **en-têtes**, une **ligne vide**, puis le **corps**.

```
Content-Type: text/plain
Status: 404 Not Found        ← optionnel : impose le code de réponse
                             ← ligne vide
...le corps...
```

On coupe à la première ligne vide (`\n\n` ou `\r\n\r\n`, on tolère les deux). On lit les en-têtes :
- **`Status:`** → fixe le **code** de la réponse HTTP (sinon `200`).
- **`Content-Type:`** → devient le `Content-Type` de notre réponse.
- les autres en-têtes sont **recopiés** dans la réponse.

Le corps du script devient le corps de notre réponse (avec un `Content-Length` **qu'on calcule** — cf. `HTTP_REQUESTS.md` §18 : **le sujet exige de dé-chunker et, si le CGI ne donne pas de `Content-Length`, c'est l'EOF qui marque la fin** — c'est exactement ce qu'on fait). Si le script ne produit **rien** → `502 Bad Gateway`.

---

## 7. Robustesse — le cœur de la note CGI

- **Un seul `poll`** : le pipe `stdout` est non-bloquant et dans l'`epoll` ; on ne `read` **jamais** sans que l'`epoll` l'ait signalé prêt. C'est la règle absolue du sujet.
- **Timeout** (`CGI_TIMEOUT`, 10 s) : un script qui ne finit jamais est un DoS. Une routine (`Cgi::routine`, appelée à chaque tour de boucle comme le gardien des clients) tue (`kill`) les CGI trop vieux et renvoie **`504 Gateway Timeout`**.
- **Pas de zombie** : chaque enfant est récupéré par `waitpid`. Sur EOF on `waitpid(WNOHANG)` ; s'il n'est pas encore mort, on l'ajoute à une liste que `Cgi::routine` **moissonne** ensuite (toujours en non-bloquant — jamais de `waitpid` bloquant qui figerait le serveur).
- **`fork` uniquement ici** : c'est le **seul** `fork` du programme (règle absolue).
- **`FD_CLOEXEC` sur le pipe** : sans lui, si **deux CGI tournent en même temps**, le second `fork` hériterait du bout de pipe du premier → l'EOF du premier n'arriverait **jamais** (son écrivain reste « ouvert » chez le second enfant). `FD_CLOEXEC` ferme automatiquement ce FD au `execve` du second. Bug classique et vicieux.
- **Déconnexion du client pendant le CGI** : le `Client` est **parqué** (`_ev.events = 0`) mais l'`epoll` signale toujours `EPOLLHUP` ; si le client part, `closeConnection` appelle `Cgi::clientGone` → on tue le script et on nettoie.
- **Suppression différée du `CgiOut`** (subtil mais critique) : quand le client part, on supprime aussi le `CgiOut` du CGI — mais ce `CgiOut` peut encore figurer **dans le même lot d'événements** que `epoll_wait` vient de rendre. Le supprimer tout de suite laisserait un **pointeur mort** dans le tableau du lot → *use-after-free* sous stress. Solution : on le **marque « mort »** (`markDead`), on le retire de l'`epoll`, et on **diffère sa destruction** à la fin du tour de boucle (`Cgi::routine`). Un event résiduel sur ce FD tombe sur `if (_dead) return;`. C'est le pendant CGI de l'invariant « ne supprimer que l'`Arole` courant » du reste du serveur (cf. `ARCHITECTURE.md` acte 7).

---

## 8. Intégration dans le flux existant

- `Client::tryStartCgi(manager)` est appelé dans `handleRead` **avant** `genResponse` : si c'est un CGI valide → on démarre le `Cgi` et on **parque** le client (`_ev.events = 0`) ; sinon on continue le flux normal (statique).
- Quand le `Cgi` a fini, `Client::deliverCgiResponse` pose la réponse dans `_response`, remet le client en `EPOLLOUT`, et la mécanique d'envoi partiel habituelle (`handleWrite`, `_bytes_send`) fait le reste — **inchangée**.
- `Cgi::routine(epoll)` est branchée dans `ServerManager::run`, juste après le gardien des timeouts clients.

Le `Router` reste **synchrone** : il ne fait que **détecter et résoudre** le CGI ; c'est le `Client` (qui a accès à l'`Epoll`) qui **orchestre** l'asynchrone.

---

## 9. Tester (les trois langages)

```sh
make re && ./Webserv configs/default.conf      # compile aussi www/cgi-bin/hello.cgi
```

```sh
# C (binaire direct)
curl -v 'http://localhost:8080/cgi-bin/hello.cgi?nom=jean'
# -> 200, corps "Hello from a C CGI!" + REQUEST_METHOD=GET + QUERY_STRING=nom=jean

# Python (interprété) -- python3 est dans le conteneur
curl -v 'http://localhost:8080/cgi-bin/hello.py?x=1'

# PHP (interprété) -- seulement si php-cgi est installé (apt install php-cgi)
curl -v 'http://localhost:8080/cgi-bin/hello.php'

# script inexistant -> 404
curl -v http://localhost:8080/cgi-bin/nope.cgi

# timeout : un script qui boucle -> 504 apres ~10 s (a ecrire pour tester)
```

**POST** (le corps est transmis au script sur son `stdin`, et renvoyé dans `BODY =`) :

```sh
curl -v -X POST --data-binary 'coucou webserv' http://localhost:8080/cgi-bin/hello.cgi
# -> 200, ... BODY = coucou webserv

curl -v -X POST --data-binary @Makefile http://localhost:8080/cgi-bin/hello.py
# -> 200, BODY = <contenu du Makefile>  (teste un gros corps ecrit par morceaux)
```

À vérifier : la `QUERY_STRING` est bien transmise, le `Content-Type` du script est respecté, et un `Status:` dans le script change bien le code HTTP.

---

## 10. Sécurité (voir aussi `SECURITY.md` §7.3)

- Le CGI **exécute du code arbitraire** : c'est la partie la plus sensible.
- On ne lance un CGI que si le script **existe** et est un **fichier régulier** ; le chemin passe par le **même anti-traversal** que le GET (`resolveCgi` → `mapPath`).
- L'environnement transmet les en-têtes en `HTTP_*` : on ne transmet que ce que le client a envoyé, tel quel.
- **Timeout + kill** empêchent un script de monopoliser le serveur.

---

## 11. POST — écrire le corps sur `stdin` (`CgiIn`)

En **POST**, `Cgi::start` garde le bout d'écriture du pipe `stdin` (au lieu de le fermer) et crée un **`CgiIn`** (intérêt `EPOLLOUT`, dans l'epoll). À chaque réveil, il écrit un morceau du corps ; **quand tout est écrit** (`Cgi::onInputDone`), il **ferme `stdin`** → le script voit EOF et peut produire sa réponse. Le corps est déjà **dé-chunké** par le parseur (le script reçoit un corps « normal », comme l'exige le sujet).

Deux points de robustesse :
- **`SIGPIPE` ignoré** (`signal(SIGPIPE, SIG_IGN)` dans `main`) : si le script ferme son `stdin` sans tout lire, notre `write` lèverait `SIGPIPE` (qui **tue** le process par défaut) ; ignoré, `write` renvoie juste une erreur et `EPOLLERR/EPOLLHUP` sur le pipe nous fait arrêter d'écrire.
- **Pas de deadlock** : `stdin` (écriture) **et** `stdout` (lecture) sont dans l'epoll simultanément ; on ne bloque jamais.

`tryStartCgi` se déclenche désormais pour **GET et POST** (un `DELETE` sur un `.cgi` reste une suppression de fichier normale).

## 12. Ce qui reste (raffinements)

- **`PATH_INFO`** ⬜ : gérer un chemin après le script (`/cgi-bin/x.cgi/a/b`).
- **`FD_CLOEXEC` global** ⬜ : le poser aussi sur l'`epoll` et les sockets d'écoute/clients pour qu'ils ne fuient pas dans le processus CGI (aujourd'hui seuls les pipes sont protégés).
