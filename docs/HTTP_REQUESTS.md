# Comprendre les requêtes HTTP (de zéro)

> Objectif : maîtriser ce qu'est une requête HTTP pour pouvoir écrire notre parseur sereinement.
> On part de rien. Chaque section finit par un encadré **« Pour notre serveur »** qui relie la théorie à ce qu'on devra coder.

---

## 1. Le décor : un dialogue client / serveur

HTTP, c'est juste **un dialogue en texte** entre deux programmes par-dessus une connexion TCP :

- le **client** (un navigateur, `curl`, telnet…) **envoie une requête** : « donne-moi cette page », « voici un fichier à enregistrer »…
- le **serveur** (nous !) **renvoie une réponse** : « voilà la page », « erreur 404 », « c'est enregistré »…

Un échange = **une requête → une réponse**. C'est tout. Le client parle d'abord, le serveur répond. Notre travail : lire la requête, la comprendre, fabriquer la bonne réponse.

> **Pour notre serveur** : on reçoit des octets (`recv`), on les interprète comme une requête, on renvoie des octets (`send`) qui forment une réponse. Le reactor (cf. `ARCHITECTURE.md`) gère déjà les octets ; ici on apprend à leur donner du sens.

---

## 2. Anatomie d'une requête : 4 parties

Une requête HTTP est du **texte structuré**. Toujours la même forme :

```
GET /index.html HTTP/1.1\r\n      ← (1) ligne de requête
Host: localhost:8080\r\n          ← (2) en-têtes (headers)
User-Agent: curl/8.0\r\n          ←     ... un par ligne
Accept: */*\r\n                   ←
\r\n                              ← (3) ligne VIDE = fin des en-têtes
                                  ← (4) corps (body) — vide pour un GET
```

Les quatre parties :

1. **La ligne de requête** (request line) — *quoi*, *où*, *quelle version*.
2. **Les en-têtes** (headers) — des métadonnées, une par ligne, format `Nom: valeur`.
3. **La ligne vide** — un `\r\n` tout seul qui marque la **fin des en-têtes**.
4. **Le corps** (optionnel) — les données envoyées (un fichier, un formulaire…). Absent pour un GET, présent pour un POST.

### Le détail qui compte : `\r\n`

Chaque ligne se termine par **deux caractères invisibles** : `\r` (retour chariot, *carriage return*, code 13) et `\n` (saut de ligne, *line feed*, code 10). On note ça **CRLF**.

La **fin des en-têtes** est donc un CRLF **vide** qui suit le dernier header : on obtient `\r\n\r\n` (le `\r\n` qui termine le dernier header, immédiatement suivi du `\r\n` de la ligne vide).

> **Pour notre serveur** : on accumule les octets jusqu'à voir `\r\n\r\n` → on sait que toute la « tête » (lignes 1+2) est arrivée et on peut la parser. Ce qui vient après `\r\n\r\n`, c'est le corps.

---

## 3. La ligne de requête, mot par mot

```
GET /index.html HTTP/1.1
└─┬─┘ └────┬────┘ └──┬───┘
méthode   cible   version
```

Trois champs **séparés par une espace** (un seul espace, code 32), terminés par CRLF :

- **Méthode** : l'action voulue (`GET`, `POST`, `DELETE`…).
- **Cible** (request-target) : sur quoi on agit, généralement un chemin (`/index.html`).
- **Version** : `HTTP/1.1` (ou `HTTP/1.0`).

> **Pour notre serveur** : on découpe cette première ligne sur les espaces → 3 morceaux. Si on n'a pas exactement 3 morceaux, ou une version inconnue → requête malformée → on répond `400 Bad Request`.

---

## 4. Les méthodes

La méthode dit **ce que le client veut faire**. Le sujet impose d'en gérer trois :

| Méthode | Sens | Corps ? | Notre rôle |
|---|---|---|---|
| **GET** | « donne-moi cette ressource » | non | lire un fichier et le renvoyer |
| **POST** | « voici des données, traite-les » | **oui** | recevoir un upload / un formulaire |
| **DELETE** | « supprime cette ressource » | non (en général) | supprimer un fichier |

Il en existe d'autres (`PUT`, `HEAD`, `OPTIONS`, `PATCH`…) qu'on n'a **pas** à implémenter. Si un client demande une méthode qu'une route n'autorise pas → on répond `405 Method Not Allowed`. Si la méthode n'existe pas du tout dans notre serveur → `501 Not Implemented`.

> **Pour notre serveur** : la liste des méthodes autorisées dépend de la **route** (config). Une même méthode peut être permise sur `/upload` et interdite sur `/images`.

---

## 5. La cible : chemin, query, encodage

La cible la plus courante (dite *origin-form*) ressemble à :

```
/dossier/page.html?nom=jean&age=24
└──────┬─────────┘ └───────┬──────┘
     chemin (path)      query string
```

- Le **chemin** (`path`) : où se trouve la ressource. C'est lui qu'on mappe vers un dossier disque (le `root` de la config : `/kapouet` → `/tmp/www`).
- La **query string** (après le `?`) : des paramètres `clé=valeur` séparés par `&`. Utile pour le CGI ; pour un fichier statique, on l'ignore.

### Le percent-encoding

Certains caractères ne peuvent pas apparaître tels quels dans une URL (espace, accents…). Ils sont encodés en `%` + code hexa : une espace devient `%20`, `é` devient `%C3%A9`. Décoder ça s'appelle l'**URL-decoding**.

> **Pour notre serveur** : il faudra décoder le `%XX` du chemin avant d'ouvrir un fichier. Et **attention sécurité** : un chemin comme `/../../etc/passwd` (path traversal) doit être bloqué — on ne doit jamais sortir du dossier autorisé.

---

## 6. Les en-têtes (headers)

Format : `Nom-Du-Header: valeur`, un par ligne. Le nom est **insensible à la casse** (`Host` = `host`). Voici ceux qui comptent pour nous :

| Header | À quoi ça sert | Importance webserv |
|---|---|---|
| **Host** | le nom de domaine + port visés (`localhost:8080`) | **obligatoire en HTTP/1.1** ; absent → `400` |
| **Content-Length** | taille du corps **en octets** | **critique** pour savoir où finit le corps |
| **Transfer-Encoding** | `chunked` = corps envoyé en morceaux | **critique** : alternative à Content-Length |
| **Content-Type** | le type du corps (`text/html`, `multipart/form-data`…) | indispensable pour les uploads |
| **Connection** | `keep-alive` (garder la connexion) ou `close` | gère la réutilisation de la connexion |
| **Expect** | `100-continue` = « j'ai un gros corps, je peux l'envoyer ? » | voir l'encadré ci-dessous |
| **Cookie** | données de session renvoyées par le client | bonus (sessions) |
| User-Agent, Accept, Referer… | infos diverses sur le client | on peut les ignorer |

> **Pour notre serveur** : on range les headers dans une `map<string, string>` (nom → valeur), en normalisant le nom en minuscules pour gérer la casse. On ne lit vraiment que ceux dont on a besoin.

### `Expect: 100-continue` : le client demande la permission avant d'envoyer le corps

Quand un client doit envoyer un gros corps, il peut ajouter `Expect: 100-continue` : il envoie **les headers seuls**, puis attend que le serveur réponde `HTTP/1.1 100 Continue` avant de transmettre le corps (curl le fait automatiquement au-delà de ~1 Ko, avec un délai de grâce d'~1s si le serveur ne dit rien).

Observé en test réel (16/07/2026, POST de 2 Mo avec `client_max_body_size 1m`) : notre serveur vérifie la limite sur le `Content-Length` **dès les headers** et répond `413` immédiatement → curl, qui attendait le feu vert, **n'a jamais envoyé les 2 Mo**. Zéro octet de body transféré pour rien : c'est le comportement optimal, et il marche « gratuitement » grâce au check anticipé.

Ce qui manque encore (prévu avec l'upload, cf. roadmap CLAUDE.md §8) : quand la requête est **acceptable**, un serveur conforme doit répondre `100 Continue` pour débloquer le client tout de suite — sans ça, curl attend son délai de grâce (~1s de latence ajoutée) avant d'envoyer le corps quand même. À implémenter : si `Expect: 100-continue` et que les checks des headers passent, émettre la ligne `HTTP/1.1 100 Continue\r\n\r\n` puis continuer à lire le corps normalement (c'est une réponse **intermédiaire** : la vraie réponse vient après).

---

## 7. La ligne vide : le séparateur

Après le dernier header, une ligne **strictement vide** (`\r\n` seul) signale : « les en-têtes sont finis, ce qui suit est le corps ». C'est le fameux `\r\n\r\n`.

> **Pour notre serveur** : c'est notre **signal de fin de tête**. Tant qu'on ne l'a pas reçu, on continue d'accumuler. Garde-fou : si la tête dépasse une taille max sans qu'on voie `\r\n\r\n`, on rejette (`400`/`431`) — sinon un client malveillant peut nous faire gonfler la mémoire à l'infini.

---

## 8. Le corps : comment savoir où il finit ? (LE point clé)

Le corps n'a **pas** de marqueur de fin comme `\r\n\r\n`. Alors comment savoir quand on a tout reçu ? **Deux mécanismes, jamais les deux à la fois :**

### Cas A — `Content-Length`

Le header annonce la taille exacte :

```
POST /upload HTTP/1.1\r\n
Host: localhost:8080\r\n
Content-Type: application/x-www-form-urlencoded\r\n
Content-Length: 27\r\n
\r\n
field1=value1&field2=value2
```

Ici `Content-Length: 27` → on lit **exactement 27 octets** de corps, puis c'est fini. Simple et déterministe.

### Cas B — `Transfer-Encoding: chunked`

Le client ne connaît pas la taille totale à l'avance (ex : données générées à la volée). Il envoie le corps **par morceaux**, chacun précédé de sa taille (en hexadécimal). Voir section 9.

### Cas C — ni l'un ni l'autre

Pour une **requête**, pas de `Content-Length` ni `chunked` → **pas de corps** (typique d'un GET/DELETE).

> **Pour notre serveur** : après avoir parsé les headers, on regarde dans cet ordre :
> 1. `Transfer-Encoding: chunked` ? → on dé-chunke (section 9).
> 2. sinon `Content-Length: N` ? → on lit N octets.
> 3. sinon → pas de corps.
> C'est ça la « phase 2 » du parseur dont on parlait : lire la tête en une fois, **puis** lire le corps selon ces règles.

---

## 9. Le `chunked` en détail

Le corps est une suite de morceaux. Chaque morceau :

```
<taille en hexa>\r\n
<données>\r\n
```

et la fin est marquée par un morceau de **taille 0** :

```
0\r\n
\r\n
```

Exemple — ce corps chunked encode le mot « Wikipedia » :

```
4\r\n
Wiki\r\n
5\r\n
pedia\r\n
0\r\n
\r\n
```

Lecture : `4` (hexa) → lis 4 octets → `Wiki`. `5` → lis 5 octets → `pedia`. `0` → fini. Résultat assemblé : `Wikipedia`.

> **Pour notre serveur** : on lit la taille (hexa) jusqu'au `\r\n`, puis ce nombre d'octets, et on recommence jusqu'au chunk `0`. Le sujet précise qu'on doit **dé-chunker** avant de passer le corps au CGI (le CGI attend un corps « normal », terminé par EOF).

---

## 10. Content-Type et l'upload de fichiers

Quand un client envoie un corps, `Content-Type` dit **comment l'interpréter**. Les plus courants :

- `application/x-www-form-urlencoded` → un formulaire simple : `champ1=valeur1&champ2=valeur2`.
- `multipart/form-data` → **l'upload de fichiers** (plusieurs champs/fichiers en un seul corps).
- `application/json` → des données JSON.
- `text/plain` → du texte brut.

Le `multipart/form-data` ressemble à ça (simplifié) :

```
Content-Type: multipart/form-data; boundary=----XYZ\r\n
Content-Length: ...\r\n
\r\n
------XYZ\r\n
Content-Disposition: form-data; name="file"; filename="photo.png"\r\n
Content-Type: image/png\r\n
\r\n
<...octets bruts du fichier...>\r\n
------XYZ--\r\n
```

Le `boundary` (annoncé dans le Content-Type) est une **chaîne séparatrice** entre les parties. Chaque partie a ses propres mini-headers (dont `filename`) puis ses données.

> **Pour notre serveur** : pour l'upload, on lit le `boundary`, on découpe le corps en parties, et pour chaque partie qui a un `filename`, on écrit les données dans le dossier d'upload de la config. C'est la partie la plus fastidieuse du POST.

---

## 11. La réponse, en miroir

Comme tu parses la requête, tu fabriqueras la réponse — même structure :

```
HTTP/1.1 200 OK\r\n              ← ligne de statut (version, code, raison)
Content-Type: text/html\r\n      ← headers
Content-Length: 1024\r\n         ← (la taille de TON corps)
\r\n                             ← ligne vide
<...le contenu HTML...>          ← corps
```

La grande différence : la première ligne est une **ligne de statut** avec un **code** (200, 404…) et une phrase (`OK`, `Not Found`). Et c'est **toi** qui dois mettre le bon `Content-Length` (la taille de ce que tu renvoies).

---

## 12. Les codes de statut utiles

| Code | Sens | Quand l'utiliser |
|---|---|---|
| **200** OK | succès | GET/DELETE réussi |
| **201** Created | créé | après un upload réussi |
| **204** No Content | succès sans corps | parfois après DELETE |
| **301 / 302** | redirection (permanente / temporaire) | règle de redirection en config |
| **400** Bad Request | requête malformée | parsing impossible |
| **403** Forbidden | accès interdit | pas les droits |
| **404** Not Found | ressource absente | fichier inexistant |
| **405** Method Not Allowed | méthode interdite **sur cette route** | GET sur une route POST-only |
| **413** Payload Too Large | corps trop gros | dépasse `client_max_body_size` |
| **500** Internal Server Error | erreur serveur | bug, CGI planté |
| **501** Not Implemented | méthode inconnue | méthode non gérée |
| **505** HTTP Version Not Supported | version refusée | autre que 1.0/1.1 |

> **Pour notre serveur** : avoir le **bon** code est noté. En cas de doute, on compare avec NGINX.

---

## 13. S'entraîner pour de vrai (le plus formateur)

La théorie rentre dix fois mieux en **regardant de vraies requêtes**. À faire, dès maintenant :

**a) Voir une requête/réponse complète avec `curl` :**
```
curl -v http://example.com
```
`-v` (verbose) affiche les lignes envoyées (`>`) et reçues (`<`). Tu vois la ligne de requête, tes headers, la ligne de statut, les headers de réponse.

**b) Écrire une requête HTTP À LA MAIN avec telnet/nc :**
```
nc example.com 80
GET / HTTP/1.1
Host: example.com
(ligne vide ici)
```
Tu tapes littéralement le protocole. C'est *exactement* ce que notre serveur recevra. Magique pour comprendre.

**c) L'onglet Network du navigateur** (F12 → Network) : clique une requête, regarde « Headers » → tu vois la vraie requête que ton navigateur envoie à ton serveur.

**d) Tester contre NGINX** : lance un NGINX, envoie-lui des requêtes bizarres, observe ses réponses/codes. C'est notre référence de comportement.

---

## 14. Les bonnes références

- **MDN — HTTP** (le meilleur point de départ, clair et en français possible) :
  - Vue d'ensemble : https://developer.mozilla.org/fr/docs/Web/HTTP
  - Anatomie des messages : https://developer.mozilla.org/fr/docs/Web/HTTP/Messages
  - Méthodes : https://developer.mozilla.org/fr/docs/Web/HTTP/Methods
  - Headers : https://developer.mozilla.org/fr/docs/Web/HTTP/Headers
  - Codes de statut : https://developer.mozilla.org/fr/docs/Web/HTTP/Status
- **HTTP Made Really Easy** (tutoriel culte des étudiants webserv, va droit au but) : https://www.jmarshall.com/easy/http/
- **Les RFC officielles** (la source de vérité, en ABNF — à consulter ponctuellement, pas à lire d'un trait) :
  - RFC 9110 — HTTP Semantics (méthodes, codes, headers) : https://www.rfc-editor.org/rfc/rfc9110
  - RFC 9112 — HTTP/1.1 (le format des messages, le chunked) : https://www.rfc-editor.org/rfc/rfc9112
  - (les anciennes RFC 7230/7231 disent la même chose, encore très citées)

Conseil : commence par **MDN** pour les concepts, garde **jmarshall** sous la main pour l'implémentation, et n'ouvre les **RFC** que pour trancher un détail précis (ex : la syntaxe exacte du chunked).

---

## 15. Ce qu'il faut retenir pour notre parseur

1. Une requête = **ligne de requête** + **headers** + **ligne vide** + **corps** (optionnel).
2. Tout est en **texte**, chaque ligne finit par **`\r\n`**.
3. La **tête** finit à `\r\n\r\n` → on accumule jusque-là, puis on parse en une fois.
4. La **ligne de requête** = `MÉTHODE cible VERSION`, séparés par des espaces.
5. Les **headers** = `Nom: valeur`, nom insensible à la casse → `map<string,string>`.
6. La **taille du corps** se déduit de `Content-Length` **ou** `Transfer-Encoding: chunked` — sinon pas de corps.
7. Toujours **valider** et répondre par le **bon code** d'erreur ; toujours mettre des **garde-fous** (taille max, path traversal) car *« resilience is key »*.

Ces 7 points sont maintenant **implémentés** : la suite de ce document dissèque le code réel.

---
---

# Deuxième partie — Notre implémentation : `Http/HttpRequest` et `Http/HttpResponse`

> La première partie donnait la théorie ; celle-ci documente **le code tel qu'il est écrit**, décision par décision. Objectif : qu'aucune ligne de `Http/` ne soit floue. Le branchement dans le moteur (qui appelle quoi, quand) est raconté dans `ARCHITECTURE.md` acte 5 — ici on ouvre le capot des deux classes elles-mêmes.

---

## 16. Vue d'ensemble : l'oreille et la voix

Deux classes, deux directions, zéro dépendance entre elles :

- **`HttpRequest`** (l'oreille) : on lui **donne des octets bruts** (`append`), elle les digère et expose deux verdicts : `isComplete()` (requête entière et valide, tous les getters sont prêts) ou `isError()` (invalide, `getErrorCode()` donne le code à renvoyer). Chaque `Client` en possède **une** (`_request`).
- **`HttpResponse`** (la voix) : on la **remplit** (`setStatus`, `setHeader`, `setBody`) puis on appelle `serialize()` qui rend la réponse complète **en une seule `std::string`**.

Pourquoi la réponse finit-elle en simple string (`Client::_response`) et pas en objet ? Parce que `handleWrite` a besoin d'un **tampon d'octets contigu et stable** pour sa mécanique d'envoi partiel (`_response.c_str() + _bytes_send`). L'objet `HttpResponse` ne vit que le temps de `genResponse` : il assemble, il sérialise, il meurt.

La contrainte qui a tout dicté côté requête : **`recv` rend ce qu'il veut**. Une requête peut arriver en un morceau, en cinquante, coupée au milieu d'un header ou d'une taille de chunk. Le parseur ne peut donc **jamais** se dire « j'attends la suite » (interdit : on ne bloque pas) — il doit pouvoir s'arrêter **à n'importe quel octet** et reprendre exactement là au `recv` suivant. C'est toute la raison d'être de la machine à états.

---

## 17. `HttpRequest` — la machine à états, pièce par pièce

### 17.1 Ce qu'elle garde en mémoire

```cpp
State                                _state;           // où en est-on ?
std::string                          _buffer;          // octets reçus PAS ENCORE consommés
std::string                          _method, _uri, _path, _query, _version;
std::map<std::string, std::string>   _headers;         // clés en minuscules
std::string                          _body;            // body assemblé (dé-chunké si besoin)
size_t                               _contentLength;   // valeur du header Content-Length
bool                                 _chunked;         // Transfer-Encoding: chunked ?
size_t                               _chunkSize;       // octets restants du chunk en cours
size_t                               _maxBodySize;     // limite (NO_BODY_LIMIT au départ, cf. 17.9)
size_t                               _headerBytes;     // taille cumulée des headers (garde-fou)
bool                                 _headersParsed;   // headers finis ? (levé par finishHeaders)
int                                  _errorCode;       // code HTTP si _state == S_ERROR
```

*(Http/HttpRequestClass.hpp)*

Distinction capitale : `_buffer` est le **sas d'entrée** (ce qui attend d'être compris), `_body` est le **résultat** (ce qui a été compris). `append` verse dans le sas ; les `stepXxx` transvasent au fur et à mesure.

### 17.2 Les états

```
S_REQUEST_LINE → S_HEADERS → S_BODY (Content-Length) ─────────────→ S_COMPLETE
                       └────→ S_CHUNK_SIZE → S_CHUNK_DATA → S_CHUNK_CRLF ┐
                                   ↑____________________________________│
                                   └──(chunk 0)→ S_TRAILER ────────────→ S_COMPLETE
                                              (à tout moment → S_ERROR)
```

| État | On attend quoi ? | On en sort quand… |
|---|---|---|
| `S_REQUEST_LINE` | la première ligne (`GET / HTTP/1.1`) | ligne complète parsée → `S_HEADERS` |
| `S_HEADERS` | des lignes `clé: valeur` | ligne **vide** → `finishHeaders()` décide de la suite |
| `S_BODY` | `_contentLength` octets bruts | tout reçu → `S_COMPLETE` |
| `S_CHUNK_SIZE` | une ligne `taille-hexa` | taille lue → `S_CHUNK_DATA` (ou `S_TRAILER` si `0`) |
| `S_CHUNK_DATA` | `_chunkSize` octets bruts | chunk entier copié → `S_CHUNK_CRLF` |
| `S_CHUNK_CRLF` | le `\r\n` qui suit les données d'un chunk | vu → retour `S_CHUNK_SIZE` |
| `S_TRAILER` | d'éventuels headers de fin (ignorés) | ligne vide → `S_COMPLETE` |
| `S_COMPLETE` / `S_ERROR` | rien — états **terminaux** | jamais (jusqu'au `reset()`) |

### 17.3 `getLine` — le découpeur de lignes (et la tolérance `\n`)

```cpp
bool HttpRequest::getLine(std::string & line)
{
    size_t pos = _buffer.find('\n');
    if (pos == std::string::npos)
        return (false);
    line = _buffer.substr(0, pos);
    _buffer.erase(0, pos + 1);
    if (!line.empty() && line[line.size() - 1] == '\r')
        line.erase(line.size() - 1);
    return (true);
}
```

*(Http/HttpRequestClass.cpp)*

- Renvoie `false` = « pas de ligne complète dans le sas » → l'état courant rend la main, on attend le prochain `recv`. C'est **le** mécanisme d'incrémentalité : partout, « il manque des octets » se traduit par un simple `return (false)` remonté jusqu'à `append`.
- On cherche `\n` et on **tolère** l'absence du `\r` (qu'on retire s'il est là). La norme dit CRLF, mais les serveurs réels (NGINX inclus) acceptent le LF nu — et c'est ce qui permet de tester à la main avec `nc` interactif, où la touche Entrée n'envoie que `\n`.
- La ligne consommée est **retirée** du sas (`erase`) : le sas ne contient jamais que du non-traité.

### 17.4 `append` — le chef de gare

```cpp
void HttpRequest::append(char const * data, size_t len)
{
    if (_state == S_COMPLETE || _state == S_ERROR)
        return ;
    _buffer.append(data, len);
    bool progress = true;
    while (progress && _state != S_COMPLETE && _state != S_ERROR)
    {
        if (_state == S_REQUEST_LINE)      progress = this->stepRequestLine();
        else if (_state == S_HEADERS)      progress = this->stepHeaders();
        else if (_state == S_BODY)         progress = this->stepBody();
        else if (_state == S_CHUNK_SIZE)   progress = this->stepChunkSize();
        else if (_state == S_CHUNK_DATA)   progress = this->stepChunkData();
        else if (_state == S_CHUNK_CRLF)   progress = this->stepChunkCrlf();
        else if (_state == S_TRAILER)      progress = this->stepTrailer();
    }
}
```

*(Http/HttpRequestClass.cpp)*

Le contrat de chaque `stepXxx` : renvoyer **`true`** = « j'ai avancé, redonne-moi la main » (la boucle re-dispatche, possiblement vers un **autre** état : c'est comme ça qu'un seul `recv` contenant toute la requête la fait traverser tous les états d'un coup) ; renvoyer **`false`** = « il me manque des octets » → la boucle s'arrête, tout l'état est déjà sauvegardé dans les membres, on reprendra au prochain `append` exactement où on en était.

Deux gardes : arrivé en `S_COMPLETE`/`S_ERROR`, `append` ne fait plus **rien** (les octets excédentaires — début d'une éventuelle requête pipelinée — restent dans `_buffer` et seront jetés au `reset`). Et la boucle s'arrête sur ces mêmes états même si `progress` est vrai.

### 17.5 La request line — fail-fast et dans quel ordre

`stepRequestLine` d'abord :

- Pas de ligne complète **et** le sas dépasse `MAX_REQUEST_LINE` (8192) → `414` : quelqu'un nous envoie une première ligne interminable, on ne va pas le laisser gonfler notre mémoire en attendant son `\n`.
- Ligne **vide** → on l'ignore et on boucle (la RFC recommande de tolérer des CRLF parasites avant la request line).
- Sinon → `parseRequestLine`, puis passage en `S_HEADERS` si pas d'erreur.

`parseRequestLine` valide **dans cet ordre**, du plus structurel au plus sémantique :

| # | Vérification | Échec |
|---|---|---|
| 1 | exactement 2 espaces → 3 champs non vides | `400` |
| 2 | méthode = uniquement des majuscules A–Z | `400` |
| 3 | aucun caractère de contrôle dans l'URI | `400` |
| 4 | version ∈ {`HTTP/1.0`, `HTTP/1.1`} | `505` |
| 5 | méthode ∈ {`GET`, `POST`, `DELETE`} | `501` |
| 6 | l'URI commence par `/` (origin-form) | `400` |
| 7 | percent-decoding du path réussi (`%` suivi de 2 hexa) | `400` |
| 8 | aucun caractère de contrôle dans le path **décodé** | `400` |

Pourquoi cet ordre ? Une ligne syntaxiquement cassée est un `400` quoi qu'il arrive (étapes 1–3) ; ensuite seulement on peut juger la sémantique : version inconnue (`505`) avant méthode inconnue (`501`), car `PUT / HTTP/9.9` est d'abord une version qu'on ne parle pas.

Le découpage cible : `?` sépare `_path` (décodé) de `_query` (gardée **brute** — le CGI voudra la `QUERY_STRING` telle quelle). L'étape 8 est une vérification **après** décodage : `%00` (NUL) ou `%0d%0a` (CRLF injecté) passeraient l'étape 3 sous leur forme encodée — décodés, ils deviennent des caractères de contrôle et sont rejetés. Le path traversal (`/../`), lui, sera bloqué au **routing** (c'est une décision d'accès disque, pas de syntaxe).

### 17.6 Les headers — ligne par ligne

`stepHeaders` tire une ligne à la fois. Deux garde-fous de taille : sas sans `\n` au-delà de `MAX_HEADER_BYTES` (32768) → `400`, et le cumul `_headerBytes` (lignes consommées) plafonné pareil — sans ça, un client enverrait des headers à l'infini. Ligne vide → `finishHeaders()` (§17.7). Sinon `parseHeaderLine` :

- `:` obligatoire, clé non vide (`400` sinon) ;
- clé : aucun caractère ≤ espace ni DEL (`400`) — un header `Ho st:` ou avec tabulation est du smuggling potentiel ;
- valeur : `trim` des espaces/tabs autour, caractères de contrôle interdits (sauf tab) ;
- clé passée **en minuscules** avant insertion dans la map — c'est comme ça qu'on gère l'insensibilité à la casse une fois pour toutes : le reste du code interroge `getHeader("host")` sans se poser la question ;
- **doublons** : `Host`, `Content-Length` et `Transfer-Encoding` en double → `400` sec. Pourquoi si dur ? Ce sont les trois headers qui déterminent *qui répond* et *où finit la requête* : deux valeurs = deux interprétations possibles = la recette du **request smuggling** (un proxy comprend l'une, nous l'autre, et un attaquant glisse une requête invisible entre les deux). Les autres headers en double sont fusionnés en `valeur1, valeur2` comme le permet la RFC.

### 17.7 `finishHeaders` — le carrefour

La ligne vide reçue, on lève `_headersParsed` (c'est ce flag que `Client` observe via `headersDone()` pour appliquer la limite, cf. 17.9) puis on décide de la suite, dans l'ordre :

1. **HTTP/1.1 sans `Host`** → `400` (obligation de la norme, et il nous le faut pour choisir le virtual host).
2. **`Transfer-Encoding` présent** : toute valeur autre que `chunked` → `501` (on ne sait pas décoder `gzip`) ; s'il y a **aussi** un `Content-Length` → `400` (les deux mécanismes en même temps = ambiguïté sur la fin du corps = smuggling, encore). Sinon → `S_CHUNK_SIZE`.
3. **`Content-Length` présent** : parsé par `parseSizeValue` (chiffres uniquement, overflow détecté — cf. 17.13) sinon `400` ; supérieur à `_maxBodySize` → **`413` immédiat, sans lire un seul octet de body** (c'est ce refus anticipé qui, combiné à `Expect: 100-continue`, a évité à curl d'envoyer ses 2 Mo au §6) ; `0` → `S_COMPLETE` direct ; sinon → `S_BODY`.
4. **Ni l'un ni l'autre** → pas de corps → `S_COMPLETE` (le cas GET/DELETE ordinaire).

### 17.8 Le corps

**Content-Length** (`stepBody`) : on copie `min(disponible, manquant)` du sas vers `_body`, et `S_COMPLETE` quand `_body.size() == _contentLength`. C'est tout — la taille a déjà été validée au carrefour.

**Chunked** — reprenons l'exemple « Wikipedia » du §9 et suivons les états :

```
"4\r\n"      S_CHUNK_SIZE : parseHexValue("4") → _chunkSize = 4        → S_CHUNK_DATA
"Wiki"       S_CHUNK_DATA : 4 octets copiés dans _body                 → S_CHUNK_CRLF
"\r\n"       S_CHUNK_CRLF : consommé, rien à garder                    → S_CHUNK_SIZE
"5\r\n"      S_CHUNK_SIZE : _chunkSize = 5                             → S_CHUNK_DATA
"pedia"      S_CHUNK_DATA : _body = "Wikipedia"                        → S_CHUNK_CRLF
"\r\n"       S_CHUNK_CRLF                                              → S_CHUNK_SIZE
"0\r\n"      S_CHUNK_SIZE : taille 0 = fin annoncée                    → S_TRAILER
"\r\n"       S_TRAILER : ligne vide                                    → S_COMPLETE
```

Détails de chaque étape :

- `stepChunkSize` : tout ce qui suit un `;` est ignoré (« extensions de chunk », prévues par la RFC, jamais utiles pour nous) ; hexa invalide → `400` ; et **la limite est vérifiée chunk par chunk** : `_body.size() + _chunkSize > _maxBodySize` → `413` (avec un test d'overflow avant l'addition — un attaquant qui annonce `FFFFFFFFFFFFFFFF` ne nous fera pas boucler l'addition).
- `stepChunkData` : même logique de copie partielle que `stepBody` — un chunk de 100 Ko coupé en dix `recv` marche tout seul, `_chunkSize` décompte ce qui manque.
- `stepChunkCrlf` : le `\r\n` qui **suit** les données est obligatoire et vérifié (`400` sinon) — c'est lui qui prouve que le client et nous sommes d'accord sur la taille annoncée.
- `stepTrailer` : après le chunk `0`, la RFC autorise des headers « de fin » ; on les lit et on les **jette** jusqu'à la ligne vide (aucun trailer ne nous intéresse, mais il faut les consommer pour trouver la vraie fin).

Le résultat : `_body` contient le corps **assemblé et dé-chunké**. Le reste du serveur (et surtout le futur CGI, qui l'exige) n'a jamais à savoir que la requête était chunked.

### 17.9 `setMaxBodySize` — la limite qui arrive « en retard », et pourquoi c'est voulu

Il y a un problème d'œuf et de poule : la limite (`client_max_body_size`) dépend du **site visé**, donc du header `Host`… qu'on ne connaît qu'une fois les headers parsés. Impossible de donner la bonne limite au parseur à sa naissance. La solution en deux temps :

1. Le parseur naît avec `_maxBodySize = NO_BODY_LIMIT` (`(size_t)-1`, « pas encore de limite »).
2. Dès que `headersDone()` passe à vrai, le `Client` choisit le virtual host via `Host` et appelle `setMaxBodySize(la vraie limite)` — qui **revalide rétroactivement** :

```cpp
void HttpRequest::setMaxBodySize(size_t limit)
{
    _maxBodySize = limit;
    if (_state == S_ERROR)
        return ;
    if (_headersParsed && !_chunked && _contentLength > limit)
    {
        this->fail(413);
        return ;
    }
    if (_body.size() > limit)
        this->fail(413);
}
```

*(Http/HttpRequestClass.cpp)*

Les deux checks rattrapent tout ce qui a pu se passer « sous » la limite infinie : un `Content-Length` annoncé trop grand, ou des octets de body déjà transvasés (possible si headers + début de body arrivent dans le même `recv`). L'exposition est bornée : entre la fin des headers et l'application de la limite, il s'écoule au plus **un** `append`, donc au plus `MAX_REQUEST` (4096) octets — jamais 2 Go.

### 17.10 Les erreurs — un seul mécanisme

```cpp
void HttpRequest::fail(int code) { _state = S_ERROR; _errorCode = code; }
```

`S_ERROR` est terminal et **absorbant** : quelle que soit la profondeur où l'erreur est détectée, tout s'arrête net (la boucle d'`append` teste l'état à chaque tour). Récapitulatif des codes émis par le parseur :

| Code | Déclencheurs |
|---|---|
| `400` | syntaxe request line / header, ctl chars, `Host` manquant (1.1), doublon Host/CL/TE, TE+CL ensemble, CL non numérique, hexa de chunk invalide, CRLF de chunk absent, headers trop gros |
| `413` | `Content-Length` > limite, ou body/chunks cumulés > limite |
| `414` | request line > 8192 octets |
| `501` | méthode hors {GET, POST, DELETE}, `Transfer-Encoding` autre que `chunked` |
| `505` | version hors {HTTP/1.0, HTTP/1.1} |

(Le `405` n'est **pas** ici : méthode *connue mais interdite sur cette route* est une décision de **routing**, pas de syntaxe.)

### 17.11 `reset` — l'astuce du keep-alive

```cpp
void HttpRequest::reset(void) { *this = HttpRequest(); }
```

Entre deux requêtes d'une même connexion, il faut remettre **tous** les membres à zéro. Plutôt que d'énumérer quinze assignations (qu'on oublierait de compléter au prochain membre ajouté), on réassigne un objet fraîchement construit : l'`operator=` généré par le compilateur copie membre à membre, et le constructeur est la **seule** source de vérité des valeurs initiales.

### 17.12 Les getters qui pensent

- `getHeader(clé)` passe la clé en minuscules avant de chercher — symétrique du rangement (§17.6) : la casse est un non-sujet des deux côtés.
- `isKeepAlive()` encode les défauts historiques du protocole : en **HTTP/1.1**, keep-alive **sauf** `Connection: close` ; en **HTTP/1.0**, close **sauf** `Connection: keep-alive`. C'est lui qui décide de `_closeAfterWrite` côté `Client`.
- `headersDone()` rend `_headersParsed` : vrai dès `finishHeaders`, même si le body est en cours — exactement l'instant où le `Client` peut lire `Host`.

### 17.13 Les helpers — petits mais pas innocents

Cinq fonctions `static` (privées au fichier, invisibles du reste du programme) : `toLowerCopy`, `trimCopy`, `hexVal`, `parseSizeValue`, `parseHexValue`, `percentDecode`. Deux méritent l'arrêt :

**Le test d'overflow** de `parseSizeValue` (et son jumeau hexa) :

```cpp
if (out > ((size_t)-1 - digit) / 10)
    return (false);
out = out * 10 + digit;
```

On veut refuser `out * 10 + digit > SIZE_MAX`… mais on ne peut pas calculer ce membre gauche : il déborderait, et en non-signé le débordement **boucle silencieusement** (`Content-Length: 18446744073709551617` deviendrait `1` — et un attaquant adorerait). D'où l'inéquation **réarrangée** pour que tout se calcule sans déborder : `out > (SIZE_MAX - digit) / 10`. Même principe en base 16 pour les tailles de chunk.

**`percentDecode`** : refuse un `%` non suivi de deux hexadécimaux (`400`). Décode tout le reste tel quel — c'est le check post-décodage de la request line (§17.5, étape 8) qui filtre les caractères dangereux, pour n'écrire la règle qu'une fois.

---

## 18. `HttpResponse` — la voix, volontairement simple

Trois membres (`_code`, `_headers`, `_body`), trois setters, et tout le travail dans `serialize` :

```cpp
oss << "HTTP/1.1 " << _code << " " << statusText(_code) << "\r\n";
oss << "Server: webserv\r\n";
oss << "Date: " << httpDate() << "\r\n";
if (_code != 204)
    oss << "Content-Length: " << _body.size() << "\r\n";
for (chaque header de la map)
    oss << clé << ": " << valeur << "\r\n";
oss << "\r\n";
if (_code != 204)
    oss << _body;
```

*(Http/HttpResponseClass.cpp)*

Les décisions cachées dans ces quelques lignes :

- **`Content-Length` est calculé, jamais fourni** : c'est `_body.size()` au moment de sérialiser. Un `Content-Length` écrit à la main finirait fatalement par mentir un jour (body modifié, page d'erreur changée…) — et un `Content-Length` faux **casse le keep-alive** : le client lit trop ou pas assez, et toutes les requêtes suivantes sur la connexion sont décalées.
- **`setBody(corps, type)` impose le `Content-Type`** en même temps que le corps : les deux n'ont aucun sens l'un sans l'autre, l'API rend l'oubli impossible.
- **`Date`** au format RFC (`Thu, 16 Jul 2026 16:05:54 GMT`) via `gmtime`/`strftime` — toujours en **GMT**, c'est la norme. Header obligatoire pour un serveur qui se respecte, et pratique en debug (on voit quelle réponse date de quand).
- **Le cas `204 No Content`** : la norme interdit tout corps → ni `Content-Length` ni body sérialisés. Prêt pour le DELETE.
- La status line annonce `HTTP/1.1` en dur : un serveur 1.1 a le droit de répondre 1.1 à un client 1.0 (c'est la version qu'on *parle*, pas un écho).
- Les headers de la map sortent triés alphabétiquement (effet de bord de `std::map`) — sans importance protocolaire, mais les réponses sont **déterministes**, ce qui simplifie les tests.

`statusText(code)` est la table officielle des raisons (`200 → OK`, `413 → Payload Too Large`…) ; `defaultErrorPage(code)` fabrique la page d'erreur HTML par défaut, calquée sur celle de NGINX — comparaison visuelle immédiate avec notre référence. Les deux sont `static` : pas besoin d'un objet pour consulter une table. Quand une `error_page` custom existera en config (routing), elle remplacera simplement l'appel à `defaultErrorPage`.

---

## 19. Synthèse : deux requêtes traversent la machine

**a) `curl` envoie un POST de 2 Mo, limite à 1 Mo** (le test réel du §6) :

1. curl envoie les headers seuls (avec `Expect: 100-continue`) et attend.
2. `recv` → `append` : `S_REQUEST_LINE` ✓ → `S_HEADERS` ✓ → `finishHeaders` : CL = 2097152, limite encore infinie → `S_BODY`.
3. Retour dans `Client::handleRead` : `headersDone()` → `selectVhost` via `Host` → `setMaxBodySize(1048576)` → revalidation : `2097152 > 1048576` → `fail(413)`.
4. `isError()` → réponse 413 sérialisée, `_draining`, `EPOLLIN|EPOLLOUT`, close après envoi. **Le body n'a jamais voyagé.**

**b) le POST chunked « Wikipedia »** : un seul `recv` contient tout → `append` traverse `S_REQUEST_LINE` → `S_HEADERS` → (`finishHeaders` voit `Transfer-Encoding: chunked`) → 3 cycles `SIZE/DATA/CRLF` → `S_TRAILER` → `S_COMPLETE`, `_body = "Wikipedia"` (9 octets — le `body_size=9` observé au test). `isKeepAlive()` vrai → réponse envoyée, `reset()`, la connexion attend la requête suivante.

Si ces deux trajets se déroulent sans zone d'ombre dans ta tête, `Http/` est à toi.

---
---

# 20. Banc d'essai — tester chaque code et chaque comportement à la main

> Ce chapitre est une **boîte à outils de tests reproductibles**. Chaque commande cible un comportement précis du parseur ou du moteur. Copier-coller, lire la réponse, comparer avec ce qui est annoncé. Toutes les commandes visent le serveur lancé avec `configs/default.conf` (endpoint `localhost:8080`, `client_max_body_size 1m` au niveau du server `localhost`).

## 20.1 Deux outils, deux usages — et pourquoi `nc` est indispensable

- **`curl`** sert à tester les cas **valides** ou **presque valides** : il construit une requête *correcte* pour nous (Host, `Content-Length`, chunked…). Parfait pour le 200, le keep-alive, les uploads, les méthodes.
- **`curl` ne peut PAS** envoyer une requête **volontairement malformée** : il refuse une version bidon, corrige les doublons, ne laisse pas injecter un caractère de contrôle. Pour tester nos `400/414/505`, il faut écrire les octets **à la main**.
- **`printf ... | nc`** est notre couteau suisse pour ça : `printf` interprète les `\r\n` littéralement, `nc` envoie les octets tels quels. On contrôle **chaque octet** de la requête.

**Le patron de base** (à réutiliser partout) :

```sh
printf 'GET / HTTP/1.1\r\nHost: localhost\r\n\r\n' | nc -w2 localhost 8080
```

- `\r\n` = le CRLF exact du protocole (§2). `\r\n\r\n` final = la ligne vide qui clôt les headers.
- `-w2` : `nc` ferme après ~2 s d'inactivité → on récupère la réponse puis la main revient (sans ça, `nc` peut rester ouvert sur une connexion keep-alive).
- Piège `printf` : un `%` littéral dans l'URL doit être **doublé** → on écrit `%%00` pour envoyer `%00`.

**Astuce lecture du code de statut** avec curl (sans afficher le corps) :

```sh
curl -s -o /dev/null -w '%{http_code}\n' http://localhost:8080/
```

---

## 20.2 Le cas nominal — `200 OK`

Rappel (§18 / `genResponse`) : tant que le routing n'existe pas, une requête valide renvoie **200** avec un **body de debug** qui récapitule ce qui a été parsé (`method=`, `path=`, `query=`, `host=`, `body_size=`). C'est notre sonde : on *voit* ce que le parseur a compris.

```sh
# GET simple
curl -v http://localhost:8080/

# path + query : vérifier le découpage ? (path décodé, query brute)
curl -v 'http://localhost:8080/dir/page.html?nom=jean&age=24'

# percent-encoding : /caf%C3%A9 doit ressortir décodé dans path=
curl -v 'http://localhost:8080/caf%C3%A9'
```

> À l'écran : `path=/caf...` décodé, `query=nom=jean&age=24` intacte. C'est le comportement des §17.5.

---

## 20.3 `400 Bad Request` — la famille la plus large

Tous ces cas exigent `nc` (curl refuserait de les produire). Chaque commande déclenche un **400** par une porte différente (cf. §17.10) :

```sh
# a) HTTP/1.1 sans Host  (obligation de la norme)
printf 'GET / HTTP/1.1\r\n\r\n' | nc -w2 localhost 8080

# b) request line mal formée (pas 3 champs)
printf 'GET /\r\n\r\n' | nc -w2 localhost 8080

# c) Host en double (smuggling)
printf 'GET / HTTP/1.1\r\nHost: a\r\nHost: b\r\n\r\n' | nc -w2 localhost 8080

# d) Content-Length en double
printf 'POST / HTTP/1.1\r\nHost: x\r\nContent-Length: 1\r\nContent-Length: 2\r\n\r\nA' | nc -w2 localhost 8080

# e) Content-Length ET Transfer-Encoding ensemble (ambiguïté = smuggling)
printf 'POST / HTTP/1.1\r\nHost: x\r\nContent-Length: 5\r\nTransfer-Encoding: chunked\r\n\r\n0\r\n\r\n' | nc -w2 localhost 8080

# f) Content-Length non numérique
printf 'POST / HTTP/1.1\r\nHost: x\r\nContent-Length: abc\r\n\r\n' | nc -w2 localhost 8080

# g) nom de header contenant une espace
printf 'GET / HTTP/1.1\r\nHost: x\r\nBad Header: v\r\n\r\n' | nc -w2 localhost 8080

# h) percent-encoding invalide (% pas suivi de 2 hexa)  -> percentDecode échoue
printf 'GET /%%ZZ HTTP/1.1\r\nHost: x\r\n\r\n' | nc -w2 localhost 8080

# i) caractère de contrôle APRÈS décodage : %00 (NUL) -> rejeté à l'étape 8 (§17.5)
printf 'GET /%%00 HTTP/1.1\r\nHost: x\r\n\r\n' | nc -w2 localhost 8080

# j) injection CRLF encodée : %0d%0a décodé devient \r\n -> caractère de contrôle -> 400
printf 'GET /a%%0d%%0ab HTTP/1.1\r\nHost: x\r\n\r\n' | nc -w2 localhost 8080

# k) taille de chunk non hexadécimale
printf 'POST / HTTP/1.1\r\nHost: x\r\nTransfer-Encoding: chunked\r\n\r\nZZ\r\n' | nc -w2 localhost 8080

# l) CRLF manquant après les données d'un chunk (on met 'XX' au lieu de \r\n)
printf 'POST / HTTP/1.1\r\nHost: x\r\nTransfer-Encoding: chunked\r\n\r\n4\r\nWikiXX5\r\n' | nc -w2 localhost 8080
```

---

## 20.4 `413 Payload Too Large` — la limite `client_max_body_size`

Le server `localhost` limite à **1 Mo**. Deux angles :

```sh
# a) Content-Length annoncé > 1 Mo : refus DÈS les headers, aucun octet de body lu
printf 'POST / HTTP/1.1\r\nHost: localhost\r\nContent-Length: 2000000\r\n\r\n' | nc -w2 localhost 8080

# b) vrai POST de 2 Mo avec curl : le body part réellement mais on répond 413 et on draine
head -c 2000000 /dev/zero | tr '\0' a | curl -v -X POST --data-binary @- http://localhost:8080/

# c) chunked qui dépasse la limite cumulée chunk par chunk (§17.8)
#    (2 Mo en un chunk : taille hexa 1E8480)
printf 'POST / HTTP/1.1\r\nHost: localhost\r\nTransfer-Encoding: chunked\r\n\r\n1E8480\r\n' | nc -w2 localhost 8080
```

> Le cas **b** combiné à `Expect: 100-continue` est le test réel du §6 (voir aussi 20.8).

---

## 20.5 `414 URI Too Long` — request line > 8192 octets

`MAX_REQUEST_LINE` = 8192. On fabrique un path de 9000 `a` :

```sh
LONG=$(head -c 9000 /dev/zero | tr '\0' a)
printf 'GET /%s HTTP/1.1\r\nHost: x\r\n\r\n' "$LONG" | nc -w2 localhost 8080
```

---

## 20.6 `501 Not Implemented` — méthode ou encodage inconnu

```sh
# a) méthode hors {GET, POST, DELETE} (curl accepte une méthode arbitraire ici)
curl -v -X PATCH http://localhost:8080/
printf 'BREW / HTTP/1.1\r\nHost: x\r\n\r\n' | nc -w2 localhost 8080

# b) Transfer-Encoding autre que chunked (on ne sait pas décoder gzip)
printf 'POST / HTTP/1.1\r\nHost: x\r\nTransfer-Encoding: gzip\r\n\r\n' | nc -w2 localhost 8080
```

> Ne pas confondre avec **405** (méthode *connue mais interdite sur la route*) : celui-là est une décision de **routing**, pas encore implémentée (§20.10).

---

## 20.7 `505 HTTP Version Not Supported` — version hors {1.0, 1.1}

Seul `nc` peut envoyer une version bidon :

```sh
printf 'GET / HTTP/9.9\r\nHost: x\r\n\r\n' | nc -w2 localhost 8080
printf 'GET / HTTP/2.0\r\nHost: x\r\n\r\n' | nc -w2 localhost 8080
```

---

## 20.8 Comportements HTTP (pas des erreurs)

### Chunked décodé correctement

```sh
# l'exemple "Wikipedia" du §9 : doit répondre 200 avec body_size=9
printf 'POST / HTTP/1.1\r\nHost: x\r\nTransfer-Encoding: chunked\r\n\r\n4\r\nWiki\r\n5\r\npedia\r\n0\r\n\r\n' | nc -w2 localhost 8080
```

### Keep-alive : réutiliser la connexion

```sh
# curl enchaîne deux requêtes sur LA MÊME connexion TCP (voir "Re-using existing connection")
curl -v http://localhost:8080/a http://localhost:8080/b
```

> **Pipelining** (deux requêtes envoyées d'un coup sans attendre la réponse) : **non supporté** — les octets de la 2ᵉ requête déjà bufferisés sont jetés au `reset()` (cf. roadmap). Donc `printf 'GET /a...\r\n\r\nGET /b...'` ne renverra **qu'une** réponse. C'est voulu : les clients standards attendent la réponse avant d'enchaîner.

### `Connection: close` force la fermeture

```sh
printf 'GET / HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n' | nc -w2 localhost 8080   # réponse Connection: close
printf 'GET / HTTP/1.0\r\nHost: x\r\n\r\n' | nc -w2 localhost 8080                          # 1.0 : close par défaut
```

### Virtual host choisi par `Host` (même port 8080)

`configs/default.conf` héberge `localhost` **et** `site-b.local` sur `:8080`. Le body de debug renvoie `host=` → on voit lequel a été ciblé :

```sh
curl -v -H 'Host: localhost'     http://localhost:8080/
curl -v -H 'Host: site-b.local'  http://localhost:8080/
```

### `Expect: 100-continue`

```sh
# état actuel : le refus anticipé marche (413 sans transfert du body),
# mais le 100 Continue conforme n'est PAS encore émis (cf. §6, roadmap)
head -c 2000000 /dev/zero | tr '\0' a | curl -v -X POST -H 'Expect: 100-continue' --data-binary @- http://localhost:8080/
```

> Attendu aujourd'hui : curl attend son délai de grâce (~1 s) puis reçoit `413`. Quand le `100 Continue` sera implémenté sur les requêtes **acceptables**, curl sera débloqué immédiatement.

### Tolérance au LF nu (test interactif au clavier)

`getLine` accepte le LF seul (§17.3). En tapant à la main, la touche Entrée n'envoie que `\n` :

```sh
nc localhost 8080
GET / HTTP/1.1        <- Entrée
Host: localhost       <- Entrée
                      <- Entrée seule (ligne vide) -> la réponse arrive
```

---

## 20.9 Timeouts — la connexion ne doit jamais hang

Trois horloges (cf. `getInactiveTime`) : `IDLE_TIMEOUT` 60 s, `HEADER_TIMEOUT` 30 s (headers incomplets, anti-slowloris), `REQUEST_TIMEOUT` 300 s. À l'échéance, le serveur **ferme la connexion** (il n'envoie pas de 504 : le client voit simplement la connexion se fermer).

```sh
# slowloris : on envoie des headers SANS la ligne vide finale, puis on attend
# -> après ~30 s (HEADER_TIMEOUT) la connexion est fermée par le serveur
(printf 'GET / HTTP/1.1\r\nHost: x\r\n'; sleep 35) | nc localhost 8080
```

---

## 20.10 Ce qui n'est PAS encore testable (arrive avec le routing)

Ces réponses **n'existent pas encore** : elles dépendent du routing (matching `location`, `root`, méthodes autorisées) et du service de fichiers. Les commandes ci-dessous sont **prêtes pour plus tard** — aujourd'hui elles renvoient le 200 de debug, pas le comportement final :

| Code / feature | Test futur | Bloqué par |
|---|---|---|
| `200` fichier statique | `curl -v http://localhost:8080/index.html` | GET statique (`stat`/`open`/MIME) |
| `404` Not Found | `curl -v http://localhost:8080/nexistepas` | routing |
| `403` Forbidden | `curl -v http://localhost:8080/prive/` | routing + droits |
| `405` Method Not Allowed | `curl -v -X POST http://localhost:8080/` (route GET-only) + header `Allow` | méthodes par location |
| `301/302` redirection | `curl -v http://localhost:8080/old` | directive `return`/redirect |
| Directory listing | `curl -v http://localhost:8080/uploads/` | autoindex |
| Upload | `curl -v -F 'file=@photo.png' http://localhost:8080/uploads` | POST + multipart |
| `DELETE` | `curl -v -X DELETE http://localhost:8080/uploads/photo.png` | handler DELETE |

---

## 20.11 Récapitulatif — une commande par code

| Code | Commande minimale |
|---|---|
| **200** | `curl -v http://localhost:8080/` |
| **400** | `printf 'GET / HTTP/1.1\r\n\r\n' \| nc -w2 localhost 8080` (Host manquant) |
| **413** | `printf 'POST / HTTP/1.1\r\nHost: localhost\r\nContent-Length: 2000000\r\n\r\n' \| nc -w2 localhost 8080` |
| **414** | `LONG=$(head -c 9000 /dev/zero \| tr '\0' a); printf 'GET /%s HTTP/1.1\r\nHost: x\r\n\r\n' "$LONG" \| nc -w2 localhost 8080` |
| **501** | `curl -v -X PATCH http://localhost:8080/` |
| **505** | `printf 'GET / HTTP/9.9\r\nHost: x\r\n\r\n' \| nc -w2 localhost 8080` |

> Règle d'or : ce qui est **valide** se teste au mieux avec `curl` ; ce qui est **volontairement cassé** exige `printf | nc`. Et en cas de doute sur un code ou un header, on compare avec **NGINX** (§13-d).
