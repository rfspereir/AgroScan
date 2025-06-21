import { Injectable } from '@angular/core';
import { HttpClient, HttpHeaders } from '@angular/common/http';
import { firstValueFrom } from 'rxjs';

@Injectable({
  providedIn: 'root'
})
export class FirebaseFunctionsService {

  // 🔥 Substitua pela URL da sua função deleteDevice (Cloud Run ou HTTPS Firebase Function)
  private deleteDeviceUrl = 'https://deletedevice-5is4ontjcq-uc.a.run.app';

  constructor(private http: HttpClient) {}

  /**
   * Chama a função deleteDevice para remover um dispositivo.
   * @param sn Número de série (SN) do dispositivo.
   * @returns Promise com a resposta da função.
   */
  async deleteDevice(sn: string) {
    const body = { sn: sn.trim().toUpperCase() };

    const headers = new HttpHeaders({
      'Content-Type': 'application/json'
      // Se você tiver autenticação, adicione aqui:
      // 'Authorization': `Bearer ${token}`
    });

    return await firstValueFrom(
      this.http.post(this.deleteDeviceUrl, body, { headers })
    );
  }
}
