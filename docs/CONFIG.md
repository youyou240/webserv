# CONFIG.md — Grammaire du fichier de configuration de webserv

> Doc de référence (« notre mini-RFC ») pour le fichier de configuration. Elle fige le **contrat** entre le parseur de fichier et le handler de requête : tant que cette spec ne change pas, le parseur et le routing peuvent évoluer indépendamment.
>
> Inspiré du bloc `server` de NGINX, mais c'est **notre** syntaxe : on n'implémente que ce qui est décrit ici.

---

## 1. Vue d'ensemble

Un fichier de config est une suite d'un ou plusieurs blocs `server`. Chaque `server` décrit un site (ou un virtual host) et contient des directives + des blocs `location`. Le fichier est lu **une seule fois** au démarrage, **entièrement** en mémoire, et tout échec de parsing est **fatal** (message + ligne, code de sortie non nul). Aucune lecture incrémentale ici (contrairement à la requête HTTP).

```
server {
    listen 8080;
    server_name site-a.com;
    root /var/www/a;
    index index.html;

    location / {
        methods GET;
    }

    location /uploads {
        methods GET POST;
        autoindex on;
        client_max_body_size 10m;
        upload_store /var/www/a/uploads;
    }
}
```

---

## 2. Lexique (tokenizer)

Le parsing se fait en **deux passes** : (1) un *lexer* transforme le texte en une liste de tokens, (2) un *parser* en descente récursive consomme ces tokens.

Caractères **structurels** (chacun = un token) :

| Token | Rôle |
|-------|------|
| `{`   | ouverture de bloc |
| `}`   | fermeture de bloc |
| `;`   | fin de directive |

Tout le reste est découpé en tokens **mot** (`WORD`) sur les blancs (espaces, tabs, retours ligne).

- **Commentaires** : `#` jusqu'à la fin de la ligne, ignorés par le lexer.
- **Blancs** : séparateurs de tokens, sans autre signification.
- Chaque token mémorise sa **ligne** (pour les messages d'erreur).

> Pas de guillemets / pas d'échappement dans cette version : un `WORD` ne contient ni blanc ni caractère structurel. (YAGNI — à ajouter seulement si un cas de test l'exige.)

---

## 3. Grammaire (ABNF)

```abnf
config         = 1*server-block

server-block   = "server" "{" *server-item "}"
server-item    = directive / location-block

location-block = "location" path "{" *directive "}"

directive      = key 1*value ";"     ; certaines directives ont un nombre d'args fixe (cf. §4)
key            = WORD
value          = WORD
path           = WORD                 ; commence par "/"
```

Notes :
- Les blancs et commentaires sont gérés par le lexer (§2), donc absents de la grammaire.
- L'arité et la sémantique de chaque directive sont validées par le parser après reconnaissance grammaticale (cf. §4).

---

## 4. Directives supportées

Légende **Niveau** : `S` = autorisé dans `server`, `L` = autorisé dans `location`.
Légende **Hérite** : la valeur du `server` sert de défaut à la `location` qui ne la définit pas (cf. §5).

| Directive | Niveau | Hérite | Arguments | Défaut |
|-----------|:------:|:------:|-----------|--------|
| `listen` | S | — | `host:port` ou `port` (host = `0.0.0.0` si absent), répétable | **requis** (≥1 par server) |
| `server_name` | S | — | 1+ noms d'hôte | aucun |
| `root` | S L | oui | 1 chemin de dossier | aucun (requête fichier → 404 si absent) |
| `index` | S L | oui | 1+ fichiers, essayés dans l'ordre | `index.html` |
| `autoindex` | S L | oui | `on` \| `off` | `off` |
| `error_page` | S L | oui (fusion) | `CODE [CODE...] /chemin` | pages internes par défaut |
| `client_max_body_size` | S L | oui | taille avec suffixe `k`/`m`/`g` ou octets | `1m` |
| `methods` | S L | oui | sous-ensemble de `GET POST DELETE` | `GET` |
| `return` | S L | oui | `CODE [URL]` (redirection 3xx ou réponse simple) | aucun |
| `upload_store` | L | — | 1 dossier de destination ; active l'upload | désactivé |
| `cgi` | S L | oui (fusion) | `.ext /chemin/interpreteur`, répétable | aucun |

### Détails

- **`listen`** : plusieurs `listen` possibles (multi-port). Les `server` qui partagent un même `host:port` sont **regroupés sur un seul socket** (cf. §6).
- **`client_max_body_size`** : suffixe insensible à la casse. `1k` = 1024, `1m` = 1024², `1g` = 1024³. Sans suffixe = octets bruts. Dépasser cette taille sur un body de requête → **413**.
- **`methods`** : une méthode hors liste reçue par le serveur → **405** (avec header `Allow`).
- **`error_page`** : `error_page 404 /errors/404.html;` ou plusieurs codes `error_page 500 502 503 /errors/5xx.html;`. Fusion à l'héritage (les codes du server sont conservés sauf ceux redéfinis par la location).
- **`return`** : `return 301 http://exemple.com/;` (redirection). Court-circuite le reste du traitement de la location.
- **`cgi`** : mappe une extension vers un interpréteur, ex. `cgi .php /usr/bin/php-cgi;`. Plusieurs `cgi` cumulent les mappings.

---

## 5. Héritage server → location

Les directives marquées **Hérite** suivent cette règle :

1. Le `server` fournit la **valeur par défaut**.
2. Une `location` qui **ne définit pas** la directive **hérite** de celle du `server`.
3. Une `location` qui **la définit** **écrase** celle du `server` (même si elle la remet à la valeur par défaut).

### Le piège : « non spécifié » ≠ « spécifié à la valeur par défaut »

Pour `autoindex` (un `bool`), `false` signifie à la fois « pas écrit » et « écrit `off` ». Ça ne suffit pas pour décider l'héritage. Solution :

- Pendant le parse, chaque champ héritable porte un **flag « a été écrit ? »** (ex. `bool _hasAutoindex;`).
- **Résolution *eager* en fin de parse** : on parcourt chaque location ; pour tout champ dont le flag est faux, on **copie la valeur du server**.
- Après résolution, chaque `LocationConfig` est **autonome** : le router ne consulte jamais le `server`. Les flags ne servent plus.

`error_page` et `cgi` se **fusionnent** plutôt que de s'écraser : on part des entrées du server, puis les entrées de la location ajoutent/remplacent par code (resp. par extension).

---

## 6. Virtual hosts (résolution par `Host`)

Deux niveaux distincts :

1. **Bind (socket)** : on **ne peut pas** `bind()` deux fois le même `host:port`. Les `server` partageant un `host:port` se résolvent en **un seul socket en écoute**. Au démarrage, le `ServerManager` regroupe les blocs par `host:port`.
2. **Sélection (requête)** : une fois la requête parsée, on lit son header `Host` et, parmi les **virtual hosts (`Server`)** attachés au socket, on choisit celui dont un `server_name` matche. Sinon → **le premier server du port** = *default server*.

Côté moteur : chaque `Socket` (via son `Acceptor`) porte la **liste des `Server`** de son endpoint (`std::vector<const Server*>`) ; le choix se fait au moment du routing. C'est en place (cf. §10).

---

## 7. Matching des `location` (longest-prefix)

Pour une URI donnée, on choisit la `location` dont le **préfixe le plus long** matche le début du path. Exemple avec `location /` et `location /uploads` : une requête `/uploads/photo.jpg` part sur `/uploads` (préfixe plus long), `/index.html` part sur `/`.

Application de `root` après matching (mapping URL→fichier) :
`location /kapouet` avec `root /tmp/www` ⇒ `/kapouet/pouic/toto/pouet` cherche `/tmp/www/pouic/toto/pouet` (on retire le préfixe de la location, on préfixe le root).

---

## 8. Gestion des erreurs de config

- **Fail-fast** : toute erreur de syntaxe ou de sémantique stoppe le démarrage.
- Message clair **avec numéro de ligne**, via une exception dédiée (`ConfigParser::parseError`), attrapée dans `main`, code de sortie non nul.
- Exemples d'erreurs fatales : accolade non fermée, directive inconnue, arité invalide, `server` sans `listen`, valeur non numérique pour un code/port/taille.

---

## 9. Modèle mémoire (structs cibles)

```
LocationConfig
    std::string                          path;                  // "/uploads"
    std::string                          root;
    std::vector<std::string>             index;
    std::set<std::string>                methods;               // GET/POST/DELETE
    bool                                 autoindex;
    std::map<int, std::string>           errorPages;
    size_t                               clientMaxBodySize;
    int                                  redirectCode;          // 0 = pas de return
    std::string                          redirectTarget;
    bool                                 uploadEnabled;
    std::string                          uploadStore;
    std::map<std::string, std::string>   cgi;                   // ".php" -> "/usr/bin/php-cgi"
    // flags d'héritage levés au parse :
    //   hasRoot, hasIndex, hasAutoindex, hasMethods, hasMaxBodySize, hasRedirect

ServerConfig
    std::vector< std::pair<std::string, uint16_t> >  listen;    // host:port
    std::vector<std::string>             serverNames;
    std::string                          root;
    std::vector<std::string>             index;
    bool                                 autoindex;
    std::map<int, std::string>           errorPages;
    size_t                               clientMaxBodySize;
    std::set<std::string>                methods;
    int                                  redirectCode;
    std::string                          redirectTarget;
    std::map<std::string, std::string>   cgi;
    std::vector<LocationConfig>          locations;
```

Tout est C++98-safe (`std::set/map/pair/vector`, `uint16_t` via `<stdint.h>`).

---

## 10. Câblage dans le moteur

`main.cpp` : `./webserv [config]` (défaut `configs/default.conf`) → `ConfigParser` produit un `std::vector<ServerConfig>`, passé au `ServerManager`.

1. **`ServerManager::build(configs)`** crée **un `Server` par bloc** (chaque `Server` copie sa `ServerConfig`).
2. Pour chaque `listen` du bloc, **`getOrCreateSocket(host, port)`** : la `map<host:port, Socket*>` du `ServerManager` (propriétaire unique des sockets) **réutilise** le socket de l'endpoint s'il existe, sinon le crée (`getaddrinfo` + `SO_REUSEADDR` + `bind`/`listen`). Les `server` partageant un endpoint tombent donc sur **un seul socket**, sans re-bind.
3. Chaque `Socket` reçoit, via son `Acceptor`, la liste des **`Server`** (virtual hosts) joignables sur son endpoint (`sock->addServer(srv)`) — base du routing par `Host`.

> Découpage : `Server` = un bloc config (un virtual host) ; `Epoll` = pur multiplexeur ; `ServerManager` = chef d'orchestre (possède les `Server` + les `Socket`, tient la boucle `run()`). Récit complet dans `ARCHITECTURE.md` (Actes 0-1).
