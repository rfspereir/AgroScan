import {onCall} from "firebase-functions/v2/https";
import * as logger from "firebase-functions/logger";
import * as admin from "firebase-admin";
import * as functions from "firebase-functions";
import cors from "cors";

admin.initializeApp();

export const createUser = onCall(async (request) => {
  const auth = request.auth;
  const data = request.data;

  if (auth?.token.role !== "root") {
    throw new Error("Acesso negado. Apenas root pode criar usuários.");
  }

  const {email, password, nome, clienteId, role} = data;

  if (!email || !password || !nome || !clienteId || !role) {
    throw new Error("Campos obrigatórios ausentes.");
  }

  const user = await admin.auth().createUser({
    email,
    password,
    displayName: nome,
  });

  await admin.auth().setCustomUserClaims(user.uid, {
    clienteId,
    role,
  });

  await admin.database()
    .ref(`clientes/${clienteId}/usuarios/${user.uid}`)
    .set({nome, email, role});

  logger.info(`Usuário criado: ${user.uid}`);

  return {uid: user.uid, message: "Usuário criado com sucesso."};
});

export const deleteUser = onCall(async (request) => {
  const auth = request.auth;
  const data = request.data;

  if (auth?.token.role !== "root") {
    throw new Error("Acesso negado. Apenas root pode excluir usuários.");
  }

  const {uid, clienteId} = data;

  if (!uid || !clienteId) {
    throw new Error("UID e clienteId são obrigatórios.");
  }

  // Remove do Auth
  await admin.auth().deleteUser(uid);

  // Remove do RTDB
  await admin.database().ref(`clientes/${clienteId}/usuarios/${uid}`).remove();

  return {message: "Usuário excluído com sucesso."};
});

export const editUser = onCall(async (request) => {
  const auth = request.auth;
  const data = request.data;

  if (auth?.token.role !== "root") {
    throw new Error("Acesso negado.");
  }

  const {uid, clienteId, nome, role} = data;

  if (!uid || !clienteId || !nome || !role) {
    throw new Error("Campos obrigatórios ausentes.");
  }

  // Atualiza no RTDB
  await admin.database().ref(`clientes/${clienteId}/usuarios/${uid}`).update({
    nome,
    role,
  });

  // Atualiza displayName no Auth (opcional)
  await admin.auth().updateUser(uid, {displayName: nome});

  return {message: "Usuário atualizado com sucesso."};
});

/**
 * Verifica se o erro possui uma propriedade 'code',
 * indicando um erro do Firebase Auth.
 * @param {unknown} err - Objeto de erro a ser verificado.
 * @return {boolean} Retorna true se o erro possui uma propriedade 'code'.
 */
function isFirebaseAuthError(err: unknown): err is { code: string } {
  return typeof err === "object" && err !== null && "code" in err;
}

/**
 * Verifica se o erro possui uma propriedade 'code',
 * indicando um erro do Firebase Auth.
 * @param {string} tamanho bjeto de erro a ser verificado.
 * @return {string} senha Retorna true se o erro possui uma propriedade 'code'.
 */
function gerarSenhaAleatoria(tamanho = 16): string {
  const caracteres = "ABCDEFGHIJKLMNOPQRSTUVWXYZ" +
  "abcdefghijklmnopqrstuvwxyz0123456789!@#$%^&*()_+-=";
  let senha = "";
  for (let i = 0; i < tamanho; i++) {
    senha += caracteres.charAt(Math.floor(Math.random() * caracteres.length));
  }
  return senha;
}

export const createDeviceV2 = functions.https.onRequest(async (req, res) => {
  if (req.method !== "POST") {
    res.status(405).send("Method Not Allowed");
    return;
  }

  const {sn, mac} = req.body;


  if (!sn || !mac) {
    res.status(400).send(
      "Campos obrigatórios ausentes: sn ou mac");
    return;
  }

  try {
    const normalizedSn = sn.trim().toUpperCase();
    const nome = "AGSN" + normalizedSn;
    const descricao = "Provisionamento Automatico";
    const senhaGerada = gerarSenhaAleatoria();


    // Busca direta no caminho mapaSn/{sn}
    const snRef = await admin.database().ref(
      `mapaSn/${normalizedSn}`).get();

    if (!snRef.exists()) {
      res.status(404).send("SN não encontrado no sistema.");
      return;
    }

    const {clienteId} = snRef.val();

    if (!clienteId) {
      res.status(400).send("ClienteId não encontrado para este SN.");
      return;
    }

    const dispositivoId = normalizedSn;
    const email = `${dispositivoId}@agroscan.com`;

    try {
      await admin.auth().createUser({
        uid: dispositivoId,
        email: email,
        password: senhaGerada});
    } catch (error) {
      if (isFirebaseAuthError(error)) {
        if (error.code !== "auth/uid-already-exists") {
          console.error("Erro ao criar usuário:", error);
          res.status(500).send("Erro ao criar usuário.");
          return;
        }
        // Se for 'auth/uid-already-exists', simplesmente ignora e segue.
      } else {
        console.error("Erro desconhecido:", error);
        res.status(500).send("Erro desconhecido ao criar usuário.");
        return;
      }
    }
    // Definir claims personalizados no usuário
    await admin.auth().setCustomUserClaims(dispositivoId, {
      clienteId,
      role: "dispositivo",
    });

    // Registrar o dispositivo no nó definitivo
    await admin.database().ref(
      `clientes/${clienteId}/dispositivos/${dispositivoId}`).set({
      mac,
      nome,
      descricao: descricao || "",
      criadoEm: Date.now(),
    });

    // Remove do mapaSn após cadastro
    await admin.database().ref(`mapaSn/${dispositivoId}`).remove();

    // Gera o custom token
    // const customToken = await admin.auth().createCustomToken(dispositivoId);

    res.status(200).send({
      uid: dispositivoId,
      clienteId: clienteId,
      email: email,
      senha: senhaGerada,
      message: "Dispositivo criado com sucesso.",
    });
  } catch (error) {
    console.error("Erro ao criar dispositivo:", JSON.stringify(error));
    res.status(500).send(JSON.stringify(error));
  }
});

const corsHandler = cors({origin: true});

export const deleteDevice = functions.https.onRequest(async (req, res) => {
  corsHandler(req, res, async () => {
    if (req.method !== "POST") {
      res.status(405).send("Method Not Allowed");
      return;
    }

    const {sn} = req.body;

    if (!sn) {
      res.status(400).send("Campo SN é obrigatório.");
      return;
    }

    const normalizedSn = sn.trim().toUpperCase();

    try {
      // Busca o clienteId no RTDB
      const snapshot = await admin.database().ref("mapaSn").orderByKey()
        .equalTo(normalizedSn).once("value");

      let clienteId = null;

      if (snapshot.exists()) {
        const dados = snapshot.val();
        const item = dados[normalizedSn];
        clienteId = item?.clienteId;
      } else {
        // Caso já não esteja no mapaSn, tenta buscar no nó definitivo
        const allClientesSnap = await admin.database().ref(
          "clientes").once("value");
        const allClientes = allClientesSnap.val();
        for (const cid in allClientes) {
          if (Object.prototype.hasOwnProperty.call(allClientes, cid)) {
            const dispositivos = allClientes[cid]?.dispositivos || {};
            if (dispositivos[normalizedSn]) {
              clienteId = cid;
              break;
            }
          }
        }
      }

      if (!clienteId) {
        res.status(404).send("Dispositivo não encontrado.");
        return;
      }

      // Deleta do Authentication
      try {
        await admin.auth().deleteUser(normalizedSn);
      } catch (err) {
        if (isFirebaseAuthError(err)) {
          if (err.code !== "auth/user-not-found") {
            throw err;
          }
          // Usuário não encontrado, ignora
        } else {
          throw err;
        }
      }

      // Deleta do RTDB
      await admin.database().ref(
        `clientes/${clienteId}/dispositivos/${normalizedSn}`
      ).remove();

      // Deleta do mapaSn, se existir
      await admin.database().ref(`mapaSn/${normalizedSn}`).remove();

      // Deletar fotos, dados, ou Storage
      await admin.storage().bucket().deleteFiles({
        prefix: `clientes/${clienteId}/dispositivos/${normalizedSn}/`,
      });

      res.status(200).send({
        sn: normalizedSn,
        clienteId,
        message: "Dispositivo excluído com sucesso.",
      });
    } catch (error) {
      console.error("Erro ao excluir dispositivo:", error);
      res.status(500).send("Erro ao excluir dispositivo.");
    }
  });
});
